package debugbridge

import (
	"bufio"
	"context"
	"encoding/base64"
	"encoding/json"
	"fmt"
	"io"
	"os"
	"os/exec"
	"path/filepath"
	"strconv"
	"strings"
	"sync"

	"github.com/Deviantedh/PVS/service/internal/model"
	"github.com/Deviantedh/PVS/service/internal/session"
)

type Config struct {
	DebuggerPath  string
	SimulatorPath string
	TempDir       string
}

type Factory struct {
	config Config
}

type Engine struct {
	cmd    *exec.Cmd
	stdin  io.WriteCloser
	lines  *bufio.Scanner
	tmpDir string
	mu     sync.Mutex
}

func NewFactory(config Config) *Factory {
	return &Factory{config: config}
}

func (f *Factory) Create(ctx context.Context, job model.Job) (session.Engine, model.SessionState, error) {
	firmware, err := base64.StdEncoding.DecodeString(job.Firmware)
	if err != nil {
		return nil, model.SessionState{}, fmt.Errorf("decode firmware: %w", err)
	}

	tmpDir, err := os.MkdirTemp(f.config.TempDir, "pvs-debug-*")
	if err != nil {
		return nil, model.SessionState{}, fmt.Errorf("create temp dir: %w", err)
	}

	firmwarePath := filepath.Join(tmpDir, "firmware.bin")
	if err := os.WriteFile(firmwarePath, firmware, 0o600); err != nil {
		_ = os.RemoveAll(tmpDir)
		return nil, model.SessionState{}, fmt.Errorf("write firmware: %w", err)
	}

	select {
	case <-ctx.Done():
		_ = os.RemoveAll(tmpDir)
		return nil, model.SessionState{}, ctx.Err()
	default:
	}

	cmd := exec.Command(f.debuggerPath(), "--firmware", firmwarePath)
	stdin, err := cmd.StdinPipe()
	if err != nil {
		_ = os.RemoveAll(tmpDir)
		return nil, model.SessionState{}, fmt.Errorf("open debugger stdin: %w", err)
	}
	stdout, err := cmd.StdoutPipe()
	if err != nil {
		_ = os.RemoveAll(tmpDir)
		return nil, model.SessionState{}, fmt.Errorf("open debugger stdout: %w", err)
	}
	cmd.Stderr = os.Stderr

	if err := cmd.Start(); err != nil {
		_ = os.RemoveAll(tmpDir)
		return nil, model.SessionState{}, fmt.Errorf("start debugger: %w", err)
	}

	engine := &Engine{
		cmd:    cmd,
		stdin:  stdin,
		lines:  bufio.NewScanner(stdout),
		tmpDir: tmpDir,
	}
	state, err := engine.readState()
	if err != nil {
		_ = engine.Close()
		return nil, model.SessionState{}, err
	}
	return engine, state, nil
}

func (f *Factory) debuggerPath() string {
	if f.config.DebuggerPath != "" {
		return f.config.DebuggerPath
	}
	if value := os.Getenv("PVS_SIM_DEBUGGER"); value != "" {
		return value
	}
	if f.config.SimulatorPath != "" {
		return filepath.Join(filepath.Dir(f.config.SimulatorPath), "pvs_sim_debug")
	}
	return "pvs_sim_debug"
}

func (e *Engine) Step(ctx context.Context, steps uint64) (model.SessionState, error) {
	if steps == 0 {
		steps = 1
	}
	return e.command(ctx, "step "+strconv.FormatUint(steps, 10))
}

func (e *Engine) Run(ctx context.Context, maxInstructions uint64) (model.SessionState, error) {
	if maxInstructions == 0 {
		maxInstructions = 1000
	}
	return e.command(ctx, "run "+strconv.FormatUint(maxInstructions, 10))
}

func (e *Engine) Stop(ctx context.Context) (model.SessionState, error) {
	return e.command(ctx, "stop")
}

func (e *Engine) Close() error {
	e.mu.Lock()
	defer e.mu.Unlock()

	if e.stdin != nil {
		_, _ = io.WriteString(e.stdin, "quit\n")
		_ = e.stdin.Close()
		e.stdin = nil
	}
	if e.cmd != nil && e.cmd.Process != nil {
		_ = e.cmd.Wait()
	}
	if e.tmpDir != "" {
		_ = os.RemoveAll(e.tmpDir)
	}
	return nil
}

func (e *Engine) command(ctx context.Context, command string) (model.SessionState, error) {
	e.mu.Lock()
	defer e.mu.Unlock()

	select {
	case <-ctx.Done():
		return model.SessionState{}, ctx.Err()
	default:
	}

	if _, err := io.WriteString(e.stdin, command+"\n"); err != nil {
		return model.SessionState{}, fmt.Errorf("write debugger command: %w", err)
	}
	return e.readStateLocked()
}

func (e *Engine) readState() (model.SessionState, error) {
	e.mu.Lock()
	defer e.mu.Unlock()
	return e.readStateLocked()
}

func (e *Engine) readStateLocked() (model.SessionState, error) {
	if !e.lines.Scan() {
		if err := e.lines.Err(); err != nil {
			return model.SessionState{}, fmt.Errorf("read debugger state: %w", err)
		}
		return model.SessionState{}, fmt.Errorf("read debugger state: %s", strings.TrimSpace("EOF"))
	}
	var state model.SessionState
	if err := json.Unmarshal(e.lines.Bytes(), &state); err != nil {
		return model.SessionState{}, fmt.Errorf("decode debugger state: %w", err)
	}
	return state, nil
}
