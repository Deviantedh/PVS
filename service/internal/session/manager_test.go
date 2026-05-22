package session

import (
	"context"
	"encoding/base64"
	"testing"

	"github.com/Deviantedh/PVS/service/internal/model"
)

func TestManagerPublishesSnapshotsAndAppliesPinOverrides(t *testing.T) {
	runner := &fakeRunner{
		result: model.Result{
			Status:               model.StatusOK,
			InstructionsExecuted: 2,
			Pins: []model.PinSnapshot{
				{Name: "PA2", Port: "A", Index: 2, Mode: "unknown", Level: nil},
			},
		},
	}
	manager := NewManager(runner)
	state, err := manager.Create(model.Job{
		JobID:    "job",
		Firmware: base64.StdEncoding.EncodeToString([]byte{0x01}),
	})
	if err != nil {
		t.Fatalf("Create returned error: %v", err)
	}

	events, cancel, err := manager.Subscribe(state.SessionID)
	if err != nil {
		t.Fatalf("Subscribe returned error: %v", err)
	}
	defer cancel()
	if initial := <-events; initial.Status != model.SessionIdle {
		t.Fatalf("unexpected initial event: %+v", initial)
	}

	state, err = manager.Step(context.Background(), state.SessionID, 2)
	if err != nil {
		t.Fatalf("Step returned error: %v", err)
	}
	if state.Status != model.SessionStopped || state.InstructionsExecuted != 2 {
		t.Fatalf("unexpected stepped state: %+v", state)
	}
	<-events // running
	if stepped := <-events; stepped.InstructionsExecuted != 2 {
		t.Fatalf("unexpected stepped event: %+v", stepped)
	}

	level := 1
	state, err = manager.SetPin(state.SessionID, "PA2", model.PinControlRequest{Level: &level})
	if err != nil {
		t.Fatalf("SetPin returned error: %v", err)
	}
	if state.Pins[0].Level == nil || *state.Pins[0].Level != 1 || state.Pins[0].Mode != "input" {
		t.Fatalf("pin override not applied: %+v", state.Pins[0])
	}
}

type fakeRunner struct {
	result model.Result
}

func (f *fakeRunner) Run(context.Context, model.Job) (model.Result, error) {
	return f.result, nil
}
