package model

const (
	StatusOK               = "OK"
	StatusTimeout          = "TIMEOUT"
	StatusCrash            = "CRASH"
	StatusUnsupportedInstr = "UNSUPPORTED_INSTR"
	StatusFault            = "FAULT"
	StatusBadInput         = "BAD_INPUT"
)

type Job struct {
	JobID    string    `json:"job_id"`
	Firmware string    `json:"firmware"`
	Config   JobConfig `json:"config"`
}

type JobConfig struct {
	MaxInstructions uint64 `json:"max_instructions"`
	TimeoutMS       int    `json:"timeout_ms"`
	UARTInput       string `json:"uart_input"`
}

type Result struct {
	JobID                string `json:"job_id"`
	Status               string `json:"status"`
	ExitCode             int    `json:"exit_code"`
	UARTOutput           string `json:"uart_output"`
	InstructionsExecuted uint64 `json:"instructions_executed"`
	Stdout               string `json:"stdout,omitempty"`
	Stderr               string `json:"stderr,omitempty"`
	Error                string `json:"error,omitempty"`
}
