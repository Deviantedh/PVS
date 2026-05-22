package session

import (
	"context"
	"crypto/rand"
	"encoding/hex"
	"errors"
	"sync"

	"github.com/Deviantedh/PVS/service/internal/model"
)

const defaultRunInstructions = 1000

var ErrNotFound = errors.New("session not found")

type Runner interface {
	Run(ctx context.Context, job model.Job) (model.Result, error)
}

type Engine interface {
	Step(ctx context.Context, steps uint64) (model.SessionState, error)
	Run(ctx context.Context, maxInstructions uint64) (model.SessionState, error)
	Stop(ctx context.Context) (model.SessionState, error)
	SetPin(ctx context.Context, name string, request model.PinControlRequest) (model.SessionState, error)
	Close() error
}

type EngineFactory interface {
	Create(ctx context.Context, job model.Job) (Engine, model.SessionState, error)
}

type Manager struct {
	factory  EngineFactory
	mu       sync.Mutex
	sessions map[string]*Session
}

type Session struct {
	id           string
	engine       Engine
	state        model.SessionState
	pinOverrides map[string]model.PinControlRequest
	subscribers  map[chan model.SessionState]struct{}
}

func NewManager(factory EngineFactory) *Manager {
	return &Manager{
		factory:  factory,
		sessions: make(map[string]*Session),
	}
}

func NewReplayManager(runner Runner) *Manager {
	return NewManager(replayFactory{runner: runner})
}

func (m *Manager) Create(ctx context.Context, job model.Job) (model.SessionState, error) {
	id, err := newID()
	if err != nil {
		return model.SessionState{}, err
	}

	engine, state, err := m.factory.Create(ctx, job)
	if err != nil {
		return model.SessionState{}, err
	}
	state.SessionID = id
	if state.Status == "" {
		state.Status = model.SessionIdle
	}
	if state.Pins == nil {
		state.Pins = []model.PinSnapshot{}
	}

	session := &Session{
		id:           id,
		engine:       engine,
		state:        state,
		pinOverrides: make(map[string]model.PinControlRequest),
		subscribers:  make(map[chan model.SessionState]struct{}),
	}

	m.mu.Lock()
	m.sessions[id] = session
	m.mu.Unlock()
	return cloneState(state), nil
}

func (m *Manager) Get(id string) (model.SessionState, error) {
	m.mu.Lock()
	defer m.mu.Unlock()

	session, ok := m.sessions[id]
	if !ok {
		return model.SessionState{}, ErrNotFound
	}
	return cloneState(session.state), nil
}

func (m *Manager) Step(ctx context.Context, id string, steps uint64) (model.SessionState, error) {
	if steps == 0 {
		steps = 1
	}
	return m.execute(ctx, id, func(engine Engine) (model.SessionState, error) {
		return engine.Step(ctx, steps)
	})
}

func (m *Manager) Run(ctx context.Context, id string, maxInstructions uint64) (model.SessionState, error) {
	if maxInstructions == 0 {
		maxInstructions = defaultRunInstructions
	}
	return m.execute(ctx, id, func(engine Engine) (model.SessionState, error) {
		return engine.Run(ctx, maxInstructions)
	})
}

func (m *Manager) Stop(ctx context.Context, id string) (model.SessionState, error) {
	return m.execute(ctx, id, func(engine Engine) (model.SessionState, error) {
		return engine.Stop(ctx)
	})
}

func (m *Manager) SetPin(ctx context.Context, id string, name string, request model.PinControlRequest) (model.SessionState, error) {
	m.mu.Lock()
	session, ok := m.sessions[id]
	if !ok {
		m.mu.Unlock()
		return model.SessionState{}, ErrNotFound
	}
	engine := session.engine
	m.mu.Unlock()

	state, err := engine.SetPin(ctx, name, request)

	m.mu.Lock()
	defer m.mu.Unlock()
	session, ok = m.sessions[id]
	if !ok {
		return model.SessionState{}, ErrNotFound
	}
	session.pinOverrides[name] = request
	if err != nil {
		session.state.Status = model.SessionFailed
		session.state.ErrorCode = model.ErrorSimulatorCrash
		session.state.Error = err.Error()
		state := cloneState(session.state)
		m.publishLocked(session, state)
		return state, nil
	}
	state.SessionID = id
	state.Pins = applyPinOverrides(state.Pins, session.pinOverrides)
	session.state = state
	out := cloneState(session.state)
	m.publishLocked(session, out)
	return out, nil
}

func (m *Manager) Subscribe(id string) (<-chan model.SessionState, func(), error) {
	m.mu.Lock()
	defer m.mu.Unlock()

	session, ok := m.sessions[id]
	if !ok {
		return nil, nil, ErrNotFound
	}

	ch := make(chan model.SessionState, 8)
	session.subscribers[ch] = struct{}{}
	ch <- cloneState(session.state)

	cancel := func() {
		m.mu.Lock()
		defer m.mu.Unlock()
		if _, ok := session.subscribers[ch]; ok {
			delete(session.subscribers, ch)
			close(ch)
		}
	}
	return ch, cancel, nil
}

func (m *Manager) execute(ctx context.Context, id string, fn func(Engine) (model.SessionState, error)) (model.SessionState, error) {
	m.mu.Lock()
	session, ok := m.sessions[id]
	if !ok {
		m.mu.Unlock()
		return model.SessionState{}, ErrNotFound
	}
	session.state.Status = model.SessionRunning
	runningState := cloneState(session.state)
	m.publishLocked(session, runningState)
	engine := session.engine
	m.mu.Unlock()

	state, err := fn(engine)

	m.mu.Lock()
	defer m.mu.Unlock()
	session, ok = m.sessions[id]
	if !ok {
		return model.SessionState{}, ErrNotFound
	}
	if err != nil {
		session.state.Status = model.SessionFailed
		session.state.ErrorCode = model.ErrorSimulatorCrash
		session.state.Error = err.Error()
		state := cloneState(session.state)
		m.publishLocked(session, state)
		return state, nil
	}

	state.SessionID = id
	state.Pins = applyPinOverrides(state.Pins, session.pinOverrides)
	session.state = state
	out := cloneState(session.state)
	m.publishLocked(session, out)
	return out, nil
}

func applyPinOverrides(pins []model.PinSnapshot, overrides map[string]model.PinControlRequest) []model.PinSnapshot {
	out := append([]model.PinSnapshot(nil), pins...)
	for i := range out {
		if override, ok := overrides[out[i].Name]; ok {
			out[i].Level = override.Level
			if override.Mode != "" {
				out[i].Mode = override.Mode
			} else {
				out[i].Mode = "input"
			}
			if override.Label != "" {
				out[i].Label = override.Label
			}
		}
	}
	return out
}

func cloneState(state model.SessionState) model.SessionState {
	state.Pins = append([]model.PinSnapshot(nil), state.Pins...)
	return state
}

func (m *Manager) publishLocked(session *Session, state model.SessionState) {
	for ch := range session.subscribers {
		select {
		case ch <- state:
		default:
		}
	}
}

func newID() (string, error) {
	var bytes [8]byte
	if _, err := rand.Read(bytes[:]); err != nil {
		return "", err
	}
	return hex.EncodeToString(bytes[:]), nil
}
