package session

import (
	"context"

	"github.com/Deviantedh/PVS/service/internal/model"
)

type replayFactory struct {
	runner Runner
}

type replayEngine struct {
	runner             Runner
	job                model.Job
	targetInstructions uint64
	state              model.SessionState
}

func (f replayFactory) Create(_ context.Context, job model.Job) (Engine, model.SessionState, error) {
	state := model.SessionState{
		Status: model.SessionIdle,
		Pins:   []model.PinSnapshot{},
	}
	return &replayEngine{
		runner: f.runner,
		job:    job,
		state:  state,
	}, state, nil
}

func (e *replayEngine) Step(ctx context.Context, steps uint64) (model.SessionState, error) {
	e.targetInstructions += steps
	return e.execute(ctx, e.targetInstructions)
}

func (e *replayEngine) Run(ctx context.Context, maxInstructions uint64) (model.SessionState, error) {
	e.targetInstructions = maxInstructions
	return e.execute(ctx, e.targetInstructions)
}

func (e *replayEngine) Stop(context.Context) (model.SessionState, error) {
	e.state.Status = model.SessionStopped
	return e.state, nil
}

func (e *replayEngine) SetPin(_ context.Context, name string, request model.PinControlRequest) (model.SessionState, error) {
	e.state.Pins = applyPinOverrides(e.state.Pins, map[string]model.PinControlRequest{name: request})
	return e.state, nil
}

func (e *replayEngine) Close() error {
	return nil
}

func (e *replayEngine) execute(ctx context.Context, target uint64) (model.SessionState, error) {
	job := e.job
	job.Config.MaxInstructions = target
	result, err := e.runner.Run(ctx, job)
	if err != nil {
		return model.SessionState{}, err
	}
	e.state = stateFromResult(result)
	return e.state, nil
}

func stateFromResult(result model.Result) model.SessionState {
	status := model.SessionStopped
	if result.Status != model.StatusOK {
		status = model.SessionFailed
	}

	return model.SessionState{
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
