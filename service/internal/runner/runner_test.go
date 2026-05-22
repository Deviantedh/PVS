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
	if result.CPU == nil || result.CPU.PC != 0x08000010 || result.CPU.InstrCount != 42 {
		t.Fatalf("CPU snapshot was not parsed: %+v", result.CPU)
	}
	if result.Peripherals == nil || result.Peripherals.TIM2.ARR != 7 || result.Peripherals.USART1.CR1 != 0x2008 {
		t.Fatalf("peripheral snapshot was not parsed: %+v", result.Peripherals)
	}
	if result.Peripherals.NVIC.Selected != 28 || len(result.Peripherals.NVIC.Enabled) != 1 || result.Peripherals.NVIC.Enabled[0] != 28 {
		t.Fatalf("NVIC snapshot was not parsed: %+v", result.Peripherals.NVIC)
	}
	if len(result.Pins) != 1 || result.Pins[0].Name != "PA2" || result.Pins[0].Level != nil || result.Pins[0].Label != "USART1_TX" {
		t.Fatalf("pin snapshot was not parsed: %+v", result.Pins)
	}
	if result.ErrorCode != "" || result.Error != "" {
		t.Fatalf("success should not carry error details: %+v", result)
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
	if result.ErrorCode != model.ErrorSubprocessTimeout {
		t.Fatalf("expected timeout error code, got %+v", result)
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
	if result.ErrorCode != model.ErrorDecodeFirmware {
		t.Fatalf("expected decode firmware error code, got %+v", result)
	}
}

func TestRunRejectsMissingJobID(t *testing.T) {
	result, err := New(Config{}).Run(context.Background(), model.Job{Firmware: base64.StdEncoding.EncodeToString([]byte{0x01})})
	if err != nil {
		t.Fatalf("Run returned setup error: %v", err)
	}
	if result.Status != model.StatusBadInput || result.ExitCode != 13 || result.ErrorCode != model.ErrorInvalidJob {
		t.Fatalf("expected invalid job result, got %+v", result)
	}
}

func TestRunReportsSimulatorExecutableError(t *testing.T) {
	job := model.Job{
		JobID:    "job-no-exe",
		Firmware: base64.StdEncoding.EncodeToString([]byte{0x01}),
	}

	result, err := New(Config{SimulatorPath: "/definitely/not/pvs_sim_cli"}).Run(context.Background(), job)
	if err != nil {
		t.Fatalf("Run returned setup error: %v", err)
	}
	if result.Status != model.StatusCrash || result.ExitCode != 12 || result.ErrorCode != model.ErrorSubprocessStart {
		t.Fatalf("expected subprocess start failure, got %+v", result)
	}
}

func TestRunReportsInvalidSimulatorResult(t *testing.T) {
	job := model.Job{
		JobID:    "job-invalid-result",
		Firmware: base64.StdEncoding.EncodeToString([]byte{0x01}),
	}

	result, err := New(fakeConfig(t, map[string]string{"PVS_FAKE_INVALID_JSON": "1"})).Run(context.Background(), job)
	if err != nil {
		t.Fatalf("Run returned setup error: %v", err)
	}
	if result.Status != model.StatusCrash || result.ExitCode != 12 || result.ErrorCode != model.ErrorInvalidSimulatorJSON {
		t.Fatalf("expected invalid simulator result, got %+v", result)
	}
}

func TestRunReportsMissingSimulatorResult(t *testing.T) {
	job := model.Job{
		JobID:    "job-missing-result",
		Firmware: base64.StdEncoding.EncodeToString([]byte{0x01}),
	}

	result, err := New(fakeConfig(t, map[string]string{"PVS_FAKE_NO_JSON": "1"})).Run(context.Background(), job)
	if err != nil {
		t.Fatalf("Run returned setup error: %v", err)
	}
	if result.Status != model.StatusCrash || result.ExitCode != 12 || result.ErrorCode != model.ErrorMissingSimulatorJSON {
		t.Fatalf("expected missing simulator result, got %+v", result)
	}
}

func TestRunClassifiesSimulatorFaultAndUnsupported(t *testing.T) {
	job := model.Job{
		JobID:    "job-fault",
		Firmware: base64.StdEncoding.EncodeToString([]byte{0x01}),
	}

	result, err := New(fakeConfig(t, map[string]string{"PVS_FAKE_STATUS": model.StatusFault, "PVS_FAKE_EXIT_CODE": "12"})).Run(context.Background(), job)
	if err != nil {
		t.Fatalf("Run returned setup error: %v", err)
	}
	if result.Status != model.StatusFault || result.ExitCode != 12 || result.ErrorCode != model.ErrorSimulatorFault {
		t.Fatalf("expected simulator fault, got %+v", result)
	}

	result, err = New(fakeConfig(t, map[string]string{"PVS_FAKE_STATUS": model.StatusUnsupportedInstr, "PVS_FAKE_EXIT_CODE": "11"})).Run(context.Background(), job)
	if err != nil {
		t.Fatalf("Run returned setup error: %v", err)
	}
	if result.Status != model.StatusUnsupportedInstr || result.ExitCode != 11 || result.ErrorCode != model.ErrorUnsupportedInstr {
		t.Fatalf("expected unsupported instruction, got %+v", result)
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

	if os.Getenv("PVS_FAKE_NO_JSON") == "1" {
		os.Exit(0)
	}

	if os.Getenv("PVS_FAKE_INVALID_JSON") == "1" {
		_ = os.WriteFile(jsonResultPath, []byte("{not-json"), 0o600)
		os.Exit(0)
	}

	instructions, _ := strconv.ParseUint(maxInstr, 10, 64)
	status := os.Getenv("PVS_FAKE_STATUS")
	if status == "" {
		status = model.StatusOK
	}
	exitCode := 0
	if code := os.Getenv("PVS_FAKE_EXIT_CODE"); code != "" {
		exitCode, _ = strconv.Atoi(code)
	}
	data, _ := json.Marshal(model.Result{
		Status:               status,
		ExitCode:             exitCode,
		UARTOutput:           "OK",
		InstructionsExecuted: instructions,
		CPU: &model.CPUSnapshot{
			PC:         0x08000010,
			MSP:        0x20000100,
			LR:         0xFFFFFFFF,
			XPSR:       0x01000000,
			PRIMASK:    0,
			InstrCount: instructions,
		},
		Peripherals: &model.PeripheralSnapshot{
			TIM2: model.TIM2Snapshot{
				ARR: 7,
				CNT: 3,
			},
			USART1: model.USART1Snapshot{
				SR:  0x80,
				CR1: 0x2008,
			},
			NVIC: model.NVICSnapshot{
				Selected: 28,
				Enabled:  []int{28},
				Pending:  []int{},
			},
		},
		Pins: []model.PinSnapshot{
			{Name: "PA2", Port: "A", Index: 2, Mode: "unknown", Level: nil, Label: "USART1_TX"},
		},
	})
	if err := os.WriteFile(jsonResultPath, data, 0o600); err != nil {
		os.Exit(12)
	}
	os.Exit(exitCode)
}

func argValue(args []string, key string) string {
	for i := 0; i < len(args)-1; i++ {
		if args[i] == key {
			return args[i+1]
		}
	}
	return ""
}
