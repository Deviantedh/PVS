package main

import (
	"context"
	"encoding/json"
	"errors"
	"flag"
	"fmt"
	"io"
	"log/slog"
	"os"

	"github.com/Deviantedh/PVS/service/internal/model"
	"github.com/Deviantedh/PVS/service/internal/otel"
	"github.com/Deviantedh/PVS/service/internal/runner"
)

func main() {
	var simulatorPath string
	var jobPath string
	var pretty bool

	flag.StringVar(&simulatorPath, "simulator", "", "path to simulator executable")
	flag.StringVar(&jobPath, "job", "-", "path to job JSON, or '-' for stdin")
	flag.BoolVar(&pretty, "pretty", false, "pretty-print JSON result")
	flag.Parse()

	ctx := context.Background()
	tracer := otel.New(slog.Default())
	end := tracer.Span(ctx, "run_simulator")
	defer end("finished")

	job, err := readJob(jobPath)
	if err != nil {
		errorCode := model.ErrorDecodeJob
		if errors.Is(err, os.ErrNotExist) {
			errorCode = model.ErrorReadJob
		}
		writeResult(model.Result{
			Status:    model.StatusBadInput,
			ExitCode:  13,
			ErrorCode: errorCode,
			Error:     err.Error(),
		}, pretty)
		os.Exit(13)
	}

	result, err := runner.New(runner.Config{SimulatorPath: simulatorPath}).Run(ctx, job)
	if err != nil {
		result = model.Result{
			JobID:     job.JobID,
			Status:    model.StatusCrash,
			ExitCode:  12,
			ErrorCode: model.ErrorSimulatorCrash,
			Error:     err.Error(),
		}
	}

	if err := writeResult(result, pretty); err != nil {
		fmt.Fprintln(os.Stderr, err)
		os.Exit(12)
	}
}

func readJob(path string) (model.Job, error) {
	var input io.Reader

	if path == "" || path == "-" {
		input = os.Stdin
	} else {
		file, err := os.Open(path)
		if err != nil {
			return model.Job{}, fmt.Errorf("open job: %w", err)
		}
		defer file.Close()
		input = file
	}

	var job model.Job
	if err := json.NewDecoder(input).Decode(&job); err != nil {
		return model.Job{}, fmt.Errorf("decode job: %w", err)
	}
	return job, nil
}

func writeResult(result model.Result, pretty bool) error {
	encoder := json.NewEncoder(os.Stdout)
	if pretty {
		encoder.SetIndent("", "  ")
	}
	return encoder.Encode(result)
}
