package runner

import (
	"bytes"
	"context"
	"encoding/base64"
	"encoding/json"
	"errors"
	"fmt"
	"os"
	"os/exec"
	"path/filepath"
	"strconv"
	"time"

	"github.com/Deviantedh/PVS/service/internal/model"
)

const (
	defaultMaxInstructions = 1_000_000
	defaultTimeoutMS       = 5_000
	defaultSimulatorPath   = "pvs_sim_cli"
)

type Config struct {
	SimulatorPath string
	WorkDir       string
	TempDir       string
	ExtraArgs     []string
	Env           []string
}

type SimulatorRunner struct {
	config Config
}

func New(config Config) *SimulatorRunner {
	return &SimulatorRunner{config: config}
}

func (r *SimulatorRunner) Run(ctx context.Context, job model.Job) (model.Result, error) {
	result := model.Result{JobID: job.JobID, Status: model.StatusCrash, ExitCode: 12}
	if job.JobID == "" {
		result.Status = model.StatusBadInput
		result.ExitCode = 13
		result.ErrorCode = model.ErrorInvalidJob
		result.Error = "job_id is required"
		return result, nil
	}

	firmware, err := base64.StdEncoding.DecodeString(job.Firmware)
	if err != nil {
		result.Status = model.StatusBadInput
		result.ExitCode = 13
		result.ErrorCode = model.ErrorDecodeFirmware
		result.Error = fmt.Sprintf("decode firmware: %v", err)
		return result, nil
	}

	timeoutMS := job.Config.TimeoutMS
	if timeoutMS <= 0 {
		timeoutMS = defaultTimeoutMS
	}
	maxInstructions := job.Config.MaxInstructions
	if maxInstructions == 0 {
		maxInstructions = defaultMaxInstructions
	}

	runCtx, cancel := context.WithTimeout(ctx, time.Duration(timeoutMS)*time.Millisecond)
	defer cancel()

	tmpDir, err := os.MkdirTemp(r.config.TempDir, "pvs-job-*")
	if err != nil {
		return result, fmt.Errorf("create temp dir: %w", err)
	}
	defer os.RemoveAll(tmpDir)

	firmwarePath := filepath.Join(tmpDir, "firmware.bin")
	jsonResultPath := filepath.Join(tmpDir, "result.json")
	if err := os.WriteFile(firmwarePath, firmware, 0o600); err != nil {
		return result, fmt.Errorf("write firmware: %w", err)
	}

	args := append([]string{}, r.config.ExtraArgs...)
	args = append(args,
		"--firmware", firmwarePath,
		"--max-instr", strconv.FormatUint(maxInstructions, 10),
		"--timeout-ms", strconv.Itoa(timeoutMS),
		"--json-result", jsonResultPath,
	)
	if job.Config.UARTInput != "" {
		args = append(args, "--uart-in", job.Config.UARTInput)
	}

	simulatorPath := r.config.SimulatorPath
	if simulatorPath == "" {
		simulatorPath = os.Getenv("PVS_SIMULATOR")
	}
	if simulatorPath == "" {
		simulatorPath = defaultSimulatorPath
	}

	cmd := exec.CommandContext(runCtx, simulatorPath, args...)
	cmd.Dir = r.config.WorkDir
	if len(r.config.Env) > 0 {
		cmd.Env = r.config.Env
	}

	var stdout bytes.Buffer
	var stderr bytes.Buffer
	cmd.Stdout = &stdout
	cmd.Stderr = &stderr

	err = cmd.Run()
	result.Stdout = stdout.String()
	result.Stderr = stderr.String()

	if errors.Is(runCtx.Err(), context.DeadlineExceeded) {
		result.Status = model.StatusTimeout
		result.ExitCode = 10
		result.ErrorCode = model.ErrorSubprocessTimeout
		result.Error = "simulator timeout"
		return result, nil
	}

	exitCode := commandExitCode(err)
	parsed, parseErr := readSimulatorResult(jsonResultPath)
	if parseErr == nil {
		result = mergeResult(result, parsed)
	}
	if result.JobID == "" {
		result.JobID = job.JobID
	}
	if result.Stdout == "" {
		result.Stdout = stdout.String()
	}
	if result.Stderr == "" {
		result.Stderr = stderr.String()
	}
	if result.UARTOutput == "" {
		result.UARTOutput = stdout.String()
	}

	if err != nil && !isExitError(err) && parseErr != nil {
		result.ExitCode = 12
		result.Status = model.StatusCrash
		result.ErrorCode = model.ErrorSubprocessStart
		result.Error = fmt.Sprintf("start simulator: %v", err)
		return result, nil
	}

	if parseErr != nil {
		result.ExitCode = exitCode
		if result.ExitCode == 0 {
			result.ExitCode = 12
			result.Status = model.StatusCrash
		} else {
			result.Status = statusFromExitCode(result.ExitCode)
		}
		result.ErrorCode = simulatorResultErrorCode(parseErr)
		if code := statusErrorCode(result.Status); code == model.ErrorUnsupportedInstr || code == model.ErrorSimulatorFault {
			result.ErrorCode = code
		}
		result.Error = parseErr.Error()
		if err != nil {
			result.Error = fmt.Sprintf("%s: %v", result.Error, err)
		}
		return result, nil
	}

	if err != nil && result.Status == model.StatusCrash {
		result.ExitCode = exitCode
		result.Status = statusFromExitCode(exitCode)
		result.ErrorCode = statusErrorCode(result.Status)
		result.Error = err.Error()
	}
	if err == nil && result.Status == model.StatusCrash {
		result.ExitCode = 0
		result.Status = model.StatusOK
	}
	if result.ErrorCode == "" {
		result.ErrorCode = statusErrorCode(result.Status)
	}

	return result, nil
}

func commandExitCode(err error) int {
	if err == nil {
		return 0
	}

	var exitErr *exec.ExitError
	if errors.As(err, &exitErr) {
		return exitErr.ExitCode()
	}
	return 12
}

func isExitError(err error) bool {
	var exitErr *exec.ExitError
	return errors.As(err, &exitErr)
}

func statusFromExitCode(code int) string {
	switch code {
	case 0:
		return model.StatusOK
	case 10:
		return model.StatusTimeout
	case 11:
		return model.StatusUnsupportedInstr
	case 12:
		return model.StatusFault
	case 13:
		return model.StatusBadInput
	default:
		return model.StatusCrash
	}
}

func statusErrorCode(status string) string {
	switch status {
	case model.StatusOK:
		return ""
	case model.StatusTimeout:
		return model.ErrorSubprocessTimeout
	case model.StatusUnsupportedInstr:
		return model.ErrorUnsupportedInstr
	case model.StatusFault:
		return model.ErrorSimulatorFault
	case model.StatusBadInput:
		return model.ErrorInvalidJob
	default:
		return model.ErrorSimulatorCrash
	}
}

func simulatorResultErrorCode(err error) string {
	if errors.Is(err, os.ErrNotExist) {
		return model.ErrorMissingSimulatorJSON
	}
	return model.ErrorInvalidSimulatorJSON
}

func readSimulatorResult(path string) (model.Result, error) {
	data, err := os.ReadFile(path)
	if err != nil {
		return model.Result{}, fmt.Errorf("read simulator result: %w", err)
	}
	if len(data) == 0 {
		return model.Result{}, errors.New("read simulator result: empty result")
	}

	var result model.Result
	if err := json.Unmarshal(data, &result); err != nil {
		return model.Result{}, fmt.Errorf("decode simulator result: %w", err)
	}
	if result.Status == "" {
		return model.Result{}, errors.New("decode simulator result: status is required")
	}
	return result, nil
}

func mergeResult(base model.Result, parsed model.Result) model.Result {
	if parsed.JobID == "" {
		parsed.JobID = base.JobID
	}
	if parsed.Status == "" {
		parsed.Status = base.Status
	}
	if parsed.Stdout == "" {
		parsed.Stdout = base.Stdout
	}
	if parsed.Stderr == "" {
		parsed.Stderr = base.Stderr
	}
	if parsed.Error == "" {
		parsed.Error = base.Error
	}
	if parsed.ErrorCode == "" {
		parsed.ErrorCode = base.ErrorCode
	}
	return parsed
}
