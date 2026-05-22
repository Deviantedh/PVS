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

type Manager struct {
	runner   Runner
	mu       sync.Mutex
	sessions map[string]*Session
}

type Session struct {
	id                 string
	job                model.Job
	targetInstructions uint64
	state              model.SessionState
	pinOverrides       map[string]model.PinControlRequest
	subscribers        map[chan model.SessionState]struct{}
}

func NewManager(runner Runner) *Manager {
	return &Manager{
		runner:   runner,
		sessions: make(map[string]*Session),
	}
}

func (m *Manager) Create(job model.Job) (model.SessionState, error) {
	id, err := newID()
	if err != nil {
		return model.SessionState{}, err
	}

	state := model.SessionState{
		SessionID: id,
		Status:    model.SessionIdle,
		Pins:      []model.PinSnapshot{},
	}
	session := &Session{
		id:           id,
		job:          job,
		state:        state,
		pinOverrides: make(map[string]model.PinControlRequest),
		subscribers:  make(map[chan model.SessionState]struct{}),
	}

	m.mu.Lock()
	m.sessions[id] = session
	m.mu.Unlock()
	return state, nil
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
	return m.execute(ctx, id, steps, false)
}

func (m *Manager) Run(ctx context.Context, id string, maxInstructions uint64) (model.SessionState, error) {
	if maxInstructions == 0 {
		maxInstructions = defaultRunInstructions
	}
	return m.execute(ctx, id, maxInstructions, true)
}

func (m *Manager) Stop(id string) (model.SessionState, error) {
	m.mu.Lock()
	defer m.mu.Unlock()

	session, ok := m.sessions[id]
	if !ok {
		return model.SessionState{}, ErrNotFound
	}
	session.state.Status = model.SessionStopped
	state := cloneState(session.state)
	m.publishLocked(session, state)
	return state, nil
}

func (m *Manager) SetPin(id string, name string, request model.PinControlRequest) (model.SessionState, error) {
	m.mu.Lock()
	defer m.mu.Unlock()

	session, ok := m.sessions[id]
	if !ok {
		return model.SessionState{}, ErrNotFound
	}
	session.pinOverrides[name] = request
	session.state.Pins = applyPinOverrides(session.state.Pins, session.pinOverrides)
	state := cloneState(session.state)
	m.publishLocked(session, state)
	return state, nil
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

func (m *Manager) execute(ctx context.Context, id string, count uint64, absolute bool) (model.SessionState, error) {
	m.mu.Lock()
	session, ok := m.sessions[id]
	if !ok {
		m.mu.Unlock()
		return model.SessionState{}, ErrNotFound
	}
	session.state.Status = model.SessionRunning
	runningState := cloneState(session.state)
	m.publishLocked(session, runningState)

	target := session.targetInstructions + count
	if absolute {
		target = count
	}
	if target == 0 {
		target = 1
	}
	job := session.job
	job.Config.MaxInstructions = target
	m.mu.Unlock()

	result, err := m.runner.Run(ctx, job)

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

	session.targetInstructions = target
	session.state = stateFromResult(id, result)
	session.state.Pins = applyPinOverrides(session.state.Pins, session.pinOverrides)
	state := cloneState(session.state)
	m.publishLocked(session, state)
	return state, nil
}

func stateFromResult(id string, result model.Result) model.SessionState {
	status := model.SessionStopped
	if result.Status != model.StatusOK {
		status = model.SessionFailed
	}

	return model.SessionState{
		SessionID:            id,
		Status:               status,
		StopReason:           result.Status,
		UARTOutput:           result.UARTOutput,
		InstructionsExecuted: result.InstructionsExecuted,
		CPU:                  result.CPU,
		Peripherals:          result.Peripherals,
		Pins:                 result.Pins,
		ErrorCode:            result.ErrorCode,
		Error:                result.Error,
	}
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
