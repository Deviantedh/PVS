package debugbridge

import (
	"context"
	"encoding/base64"
	"os"
	"path/filepath"
	"testing"

	"github.com/Deviantedh/PVS/service/internal/model"
)

func TestProcessBridgeKeepsStateBetweenSteps(t *testing.T) {
	debuggerPath := os.Getenv("PVS_SIM_DEBUGGER_TEST")
	if debuggerPath == "" {
		t.Skip("PVS_SIM_DEBUGGER_TEST is not set")
	}

	factory := NewFactory(Config{DebuggerPath: debuggerPath})
	engine, initial, err := factory.Create(context.Background(), model.Job{
		JobID:    "debug-test",
		Firmware: base64.StdEncoding.EncodeToString(testFirmware()),
	})
	if err != nil {
		t.Fatalf("Create returned error: %v", err)
	}
	defer engine.Close()

	if initial.Status != model.SessionIdle || initial.CPU == nil {
		t.Fatalf("unexpected initial state: %+v", initial)
	}

	one, err := engine.Step(context.Background(), 1)
	if err != nil {
		t.Fatalf("Step(1) returned error: %v", err)
	}
	two, err := engine.Step(context.Background(), 1)
	if err != nil {
		t.Fatalf("second Step(1) returned error: %v", err)
	}
	if one.InstructionsExecuted != 1 || two.InstructionsExecuted != 2 {
		t.Fatalf("state was not preserved between steps: one=%+v two=%+v", one, two)
	}
	if one.CPU == nil || two.CPU == nil || one.CPU.PC == two.CPU.PC {
		t.Fatalf("expected PC to advance across stateful steps: one=%+v two=%+v", one.CPU, two.CPU)
	}

	ran, err := engine.Run(context.Background(), 3)
	if err != nil {
		t.Fatalf("Run returned error: %v", err)
	}
	if ran.InstructionsExecuted != 5 {
		t.Fatalf("run should continue existing state, got %+v", ran)
	}

	stopped, err := engine.Stop(context.Background())
	if err != nil {
		t.Fatalf("Stop returned error: %v", err)
	}
	if stopped.Status != model.SessionStopped || stopped.InstructionsExecuted != ran.InstructionsExecuted {
		t.Fatalf("unexpected stopped state: %+v", stopped)
	}
}

func TestDebuggerPathFallsBackToSimulatorSibling(t *testing.T) {
	path := NewFactory(Config{SimulatorPath: filepath.Join("build", "sim", "pvs_sim_cli")}).debuggerPath()
	if filepath.Base(path) != "pvs_sim_debug" {
		t.Fatalf("unexpected debugger path: %s", path)
	}
}

func testFirmware() []byte {
	return []byte{
		0x00, 0x01, 0x00, 0x20,
		0x09, 0x00, 0x00, 0x08,
		0x00, 0xbf,
		0x00, 0xbf,
		0xfe, 0xe7,
	}
}
