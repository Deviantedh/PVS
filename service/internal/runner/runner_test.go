package runner

import (
	"context"
	"encoding/base64"
	"encoding/json"
	"os"
	"strconv"
	"testing"
	"time"

	"github.com/Deviantedh/PVS/service/internal/model"
)

func TestRunUsesSimulatorContract(t *testing.T) {
	job := model.Job{
		JobID:    "job-1",
		Firmware: base64.StdEncoding.EncodeToString([]byte{0x01, 0x02, 0x03}),
		Config: model.JobConfig{
			MaxInstructions: 42,
			TimeoutMS:       1000,
			UARTInput:       "rx",
		},
	}

	result, err := New(fakeConfig(t, nil)).Run(context.Background(), job)
	if err != nil {
		t.Fatalf("Run returned error: %v", err)
	}
	if result.JobID != job.JobID || result.Status != model.StatusOK || result.ExitCode != 0 {
		t.Fatalf("unexpected result: %+v", result)
	}
	if result.UARTOutput != "OK" || result.InstructionsExecuted != 42 {
		t.Fatalf("simulator result was not parsed: %+v", result)
	}
}

func TestRunTimesOutSimulator(t *testing.T) {
	job := model.Job{
		JobID:    "job-timeout",
		Firmware: base64.StdEncoding.EncodeToString([]byte{0x01}),
		Config: model.JobConfig{
			TimeoutMS: 50,
		},
	}

	result, err := New(fakeConfig(t, map[string]string{"PVS_FAKE_SLEEP_MS": "250"})).Run(context.Background(), job)
	if err != nil {
		t.Fatalf("Run returned error: %v", err)
	}
	if result.Status != model.StatusTimeout || result.ExitCode != 10 {
		t.Fatalf("expected timeout result, got %+v", result)
	}
}

func TestRunRejectsBadFirmware(t *testing.T) {
	result, err := New(Config{}).Run(context.Background(), model.Job{JobID: "bad", Firmware: "not-base64"})
	if err != nil {
		t.Fatalf("Run returned setup error: %v", err)
	}
	if result.Status != model.StatusBadInput || result.ExitCode != 13 {
		t.Fatalf("expected bad input, got %+v", result)
	}
}

func fakeConfig(t *testing.T, env map[string]string) Config {
	t.Helper()

	values := append(os.Environ(), "PVS_FAKE_SIMULATOR=1")
	for key, value := range env {
		values = append(values, key+"="+value)
	}

	return Config{
		SimulatorPath: os.Args[0],
		ExtraArgs:     []string{"-test.run=TestHelperProcess", "--"},
		Env:           values,
	}
}

func TestHelperProcess(t *testing.T) {
	if os.Getenv("PVS_FAKE_SIMULATOR") != "1" {
		return
	}

	args := os.Args
	for len(args) > 0 && args[0] != "--" {
		args = args[1:]
	}
	if len(args) > 0 {
		args = args[1:]
	}

	if sleep := os.Getenv("PVS_FAKE_SLEEP_MS"); sleep != "" {
		ms, _ := strconv.Atoi(sleep)
		time.Sleep(time.Duration(ms) * time.Millisecond)
	}

	firmwarePath := argValue(args, "--firmware")
	jsonResultPath := argValue(args, "--json-result")
	maxInstr := argValue(args, "--max-instr")
	if firmwarePath == "" || jsonResultPath == "" || maxInstr == "" {
		os.Exit(13)
	}
	if _, err := os.ReadFile(firmwarePath); err != nil {
		os.Exit(13)
	}

	instructions, _ := strconv.ParseUint(maxInstr, 10, 64)
	data, _ := json.Marshal(model.Result{
		Status:               model.StatusOK,
		ExitCode:             0,
		UARTOutput:           "OK",
		InstructionsExecuted: instructions,
	})
	if err := os.WriteFile(jsonResultPath, data, 0o600); err != nil {
		os.Exit(12)
	}
	os.Exit(0)
}

func argValue(args []string, key string) string {
	for i := 0; i < len(args)-1; i++ {
		if args[i] == key {
			return args[i+1]
		}
	}
	return ""
}
