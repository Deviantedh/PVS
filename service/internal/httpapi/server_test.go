package httpapi

import (
	"bytes"
	"context"
	"encoding/base64"
	"encoding/json"
	"errors"
	"net/http"
	"net/http/httptest"
	"testing"
	"time"

	"github.com/Deviantedh/PVS/service/internal/model"
)

func TestPostRunSuccess(t *testing.T) {
	runner := &fakeRunner{
		result: model.Result{
			JobID:                "job-1",
			Status:               model.StatusOK,
			ExitCode:             0,
			UARTOutput:           "T",
			InstructionsExecuted: 12,
			CPU: &model.CPUSnapshot{
				PC:         0x08000020,
				MSP:        0x20000100,
				LR:         0xFFFFFFF9,
				XPSR:       0x01000000,
				InstrCount: 12,
			},
			Peripherals: &model.PeripheralSnapshot{
				TIM2:   model.TIM2Snapshot{CR1: 1, ARR: 4, CNT: 2, DIER: 1, SR: 1},
				USART1: model.USART1Snapshot{SR: 0x80, CR1: 0x2008},
				NVIC:   model.NVICSnapshot{Selected: -1, Enabled: []int{28}, Pending: []int{}},
			},
			Pins: []model.PinSnapshot{
				{Name: "PA2", Port: "A", Index: 2, Mode: "unknown", Level: nil, Label: "USART1_TX"},
			},
		},
	}
	server := NewServer(runner, nil)

	response := postJSON(t, server.Handler(), model.Job{
		JobID:    "job-1",
		Firmware: base64.StdEncoding.EncodeToString([]byte{0x01}),
	})

	if response.Code != http.StatusOK {
		t.Fatalf("expected 200, got %d: %s", response.Code, response.Body.String())
	}
	result := decodeResult(t, response)
	if result.Status != model.StatusOK || result.UARTOutput != "T" || result.InstructionsExecuted != 12 {
		t.Fatalf("unexpected result: %+v", result)
	}
	if result.CPU == nil || result.CPU.PC != 0x08000020 {
		t.Fatalf("HTTP response lost CPU snapshot: %+v", result.CPU)
	}
	if result.Peripherals == nil || result.Peripherals.TIM2.CR1 != 1 || result.Peripherals.USART1.CR1 != 0x2008 {
		t.Fatalf("HTTP response lost peripheral snapshot: %+v", result.Peripherals)
	}
	if !bytes.Contains(response.Body.Bytes(), []byte(`"pins":[`)) || !bytes.Contains(response.Body.Bytes(), []byte(`"name":"PA2"`)) {
		t.Fatalf("HTTP response should preserve pins array: %s", response.Body.String())
	}
	if len(result.Pins) != 1 || result.Pins[0].Port != "A" || result.Pins[0].Level != nil {
		t.Fatalf("unexpected pins snapshot: %+v", result.Pins)
	}
	if runner.seen.JobID != "job-1" {
		t.Fatalf("runner did not receive job: %+v", runner.seen)
	}
}

func TestPostRunRejectsBadJSON(t *testing.T) {
	server := NewServer(&fakeRunner{}, nil)
	request := httptest.NewRequest(http.MethodPost, "/api/run", bytes.NewBufferString("{not-json"))
	response := httptest.NewRecorder()

	server.Handler().ServeHTTP(response, request)

	if response.Code != http.StatusBadRequest {
		t.Fatalf("expected 400, got %d", response.Code)
	}
	result := decodeResult(t, response)
	if result.Status != model.StatusBadInput || result.ErrorCode != model.ErrorDecodeJob {
		t.Fatalf("expected decode job error, got %+v", result)
	}
}

func TestPostRunRejectsMissingRequiredFields(t *testing.T) {
	server := NewServer(&fakeRunner{}, nil)
	response := postJSON(t, server.Handler(), model.Job{JobID: "job-no-firmware"})

	if response.Code != http.StatusBadRequest {
		t.Fatalf("expected 400, got %d", response.Code)
	}
	result := decodeResult(t, response)
	if result.Status != model.StatusBadInput || result.ErrorCode != model.ErrorInvalidJob {
		t.Fatalf("expected invalid job error, got %+v", result)
	}
}

func TestPostRunReturnsRunnerError(t *testing.T) {
	server := NewServer(&fakeRunner{err: errors.New("spawn failed")}, nil)
	response := postJSON(t, server.Handler(), model.Job{
		JobID:    "job-crash",
		Firmware: base64.StdEncoding.EncodeToString([]byte{0x01}),
	})

	if response.Code != http.StatusBadGateway {
		t.Fatalf("expected 502, got %d", response.Code)
	}
	result := decodeResult(t, response)
	if result.Status != model.StatusCrash || result.ErrorCode != model.ErrorSimulatorCrash {
		t.Fatalf("expected simulator crash, got %+v", result)
	}
}

func TestPostRunReturnsTimeoutStatus(t *testing.T) {
	server := NewServer(&fakeRunner{result: model.Result{
		JobID:     "job-timeout",
		Status:    model.StatusTimeout,
		ExitCode:  10,
		ErrorCode: model.ErrorSubprocessTimeout,
		Error:     "simulator timeout",
	}}, nil)
	response := postJSON(t, server.Handler(), model.Job{
		JobID:    "job-timeout",
		Firmware: base64.StdEncoding.EncodeToString([]byte{0x01}),
	})

	if response.Code != http.StatusGatewayTimeout {
		t.Fatalf("expected 504, got %d", response.Code)
	}
	result := decodeResult(t, response)
	if result.Status != model.StatusTimeout || result.ErrorCode != model.ErrorSubprocessTimeout {
		t.Fatalf("expected timeout, got %+v", result)
	}
}

func TestSessionStepAndPinControl(t *testing.T) {
	runner := &fakeRunner{
		result: model.Result{
			Status:               model.StatusOK,
			ExitCode:             0,
			InstructionsExecuted: 3,
			Pins: []model.PinSnapshot{
				{Name: "PA2", Port: "A", Index: 2, Mode: "unknown", Level: nil, Label: "USART1_TX"},
			},
		},
	}
	server := NewServer(runner, nil)
	handler := server.Handler()

	createResponse := postJSONToPath(t, handler, "/api/session", model.Job{
		JobID:    "session-job",
		Firmware: base64.StdEncoding.EncodeToString([]byte{0x01}),
	})
	if createResponse.Code != http.StatusCreated {
		t.Fatalf("expected 201, got %d: %s", createResponse.Code, createResponse.Body.String())
	}
	state := decodeSessionState(t, createResponse)
	if state.SessionID == "" || state.Status != model.SessionIdle {
		t.Fatalf("unexpected created session state: %+v", state)
	}

	stepResponse := postJSONToPath(t, handler, "/api/session/"+state.SessionID+"/step", model.StepRequest{Steps: 3})
	if stepResponse.Code != http.StatusOK {
		t.Fatalf("expected 200, got %d: %s", stepResponse.Code, stepResponse.Body.String())
	}
	state = decodeSessionState(t, stepResponse)
	if state.Status != model.SessionStopped || state.InstructionsExecuted != 3 || len(state.Pins) != 1 {
		t.Fatalf("unexpected stepped session state: %+v", state)
	}
	if runner.seen.Config.MaxInstructions != 3 {
		t.Fatalf("step did not set runner max instructions: %+v", runner.seen.Config)
	}

	level := 1
	pinResponse := postJSONToPath(t, handler, "/api/session/"+state.SessionID+"/pins/PA2", model.PinControlRequest{Level: &level})
	if pinResponse.Code != http.StatusOK {
		t.Fatalf("expected 200, got %d: %s", pinResponse.Code, pinResponse.Body.String())
	}
	state = decodeSessionState(t, pinResponse)
	if len(state.Pins) != 1 || state.Pins[0].Level == nil || *state.Pins[0].Level != 1 || state.Pins[0].Mode != "input" {
		t.Fatalf("pin control was not reflected in snapshot: %+v", state.Pins)
	}
}

func TestSessionEventsReturnsInitialSSESnapshot(t *testing.T) {
	server := NewServer(&fakeRunner{}, nil)
	handler := server.Handler()

	createResponse := postJSONToPath(t, handler, "/api/session", model.Job{
		JobID:    "events-job",
		Firmware: base64.StdEncoding.EncodeToString([]byte{0x01}),
	})
	state := decodeSessionState(t, createResponse)

	ctx, cancel := context.WithTimeout(context.Background(), 10*time.Millisecond)
	defer cancel()
	request := httptest.NewRequestWithContext(ctx, http.MethodGet, "/api/session/"+state.SessionID+"/events", nil)
	response := httptest.NewRecorder()
	handler.ServeHTTP(response, request)

	if response.Code != http.StatusOK {
		t.Fatalf("expected 200, got %d: %s", response.Code, response.Body.String())
	}
	if !bytes.Contains(response.Body.Bytes(), []byte("event: snapshot")) || !bytes.Contains(response.Body.Bytes(), []byte(state.SessionID)) {
		t.Fatalf("expected initial SSE snapshot, got %s", response.Body.String())
	}
}

type fakeRunner struct {
	result model.Result
	err    error
	seen   model.Job
}

func (f *fakeRunner) Run(_ context.Context, job model.Job) (model.Result, error) {
	f.seen = job
	if f.err != nil {
		return model.Result{}, f.err
	}
	return f.result, nil
}

func postJSON(t *testing.T, handler http.Handler, body any) *httptest.ResponseRecorder {
	t.Helper()
	return postJSONToPath(t, handler, "/api/run", body)
}

func postJSONToPath(t *testing.T, handler http.Handler, path string, body any) *httptest.ResponseRecorder {
	t.Helper()

	data, err := json.Marshal(body)
	if err != nil {
		t.Fatalf("marshal request: %v", err)
	}
	request := httptest.NewRequest(http.MethodPost, path, bytes.NewReader(data))
	request.Header.Set("Content-Type", "application/json")
	response := httptest.NewRecorder()
	handler.ServeHTTP(response, request)
	return response
}

func decodeResult(t *testing.T, response *httptest.ResponseRecorder) model.Result {
	t.Helper()

	var result model.Result
	if err := json.Unmarshal(response.Body.Bytes(), &result); err != nil {
		t.Fatalf("decode result: %v; body=%s", err, response.Body.String())
	}
	return result
}

func decodeSessionState(t *testing.T, response *httptest.ResponseRecorder) model.SessionState {
	t.Helper()

	var state model.SessionState
	if err := json.Unmarshal(response.Body.Bytes(), &state); err != nil {
		t.Fatalf("decode session state: %v; body=%s", err, response.Body.String())
	}
	return state
}
