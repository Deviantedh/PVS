package model

const (
	StatusOK               = "OK"
	StatusTimeout          = "TIMEOUT"
	StatusCrash            = "CRASH"
	StatusUnsupportedInstr = "UNSUPPORTED_INSTR"
	StatusFault            = "FAULT"
	StatusBadInput         = "BAD_INPUT"
)

const (
	ErrorReadJob              = "READ_JOB"
	ErrorDecodeJob            = "DECODE_JOB"
	ErrorDecodeFirmware       = "DECODE_FIRMWARE"
	ErrorInvalidJob           = "INVALID_JOB"
	ErrorSubprocessStart      = "SUBPROCESS_START"
	ErrorSubprocessTimeout    = "SUBPROCESS_TIMEOUT"
	ErrorInvalidSimulatorJSON = "INVALID_SIMULATOR_JSON"
	ErrorMissingSimulatorJSON = "MISSING_SIMULATOR_JSON"
	ErrorSimulatorFault       = "SIMULATOR_FAULT"
	ErrorUnsupportedInstr     = "UNSUPPORTED_INSTR"
	ErrorSimulatorCrash       = "SIMULATOR_CRASH"
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
	JobID                string              `json:"job_id"`
	Status               string              `json:"status"`
	ExitCode             int                 `json:"exit_code"`
	UARTOutput           string              `json:"uart_output"`
	InstructionsExecuted uint64              `json:"instructions_executed"`
	CPU                  *CPUSnapshot        `json:"cpu,omitempty"`
	Peripherals          *PeripheralSnapshot `json:"peripherals,omitempty"`
	Pins                 []PinSnapshot       `json:"pins"`
	Stdout               string              `json:"stdout,omitempty"`
	Stderr               string              `json:"stderr,omitempty"`
	ErrorCode            string              `json:"error_code,omitempty"`
	Error                string              `json:"error,omitempty"`
}

type CPUSnapshot struct {
	PC         uint32 `json:"pc"`
	MSP        uint32 `json:"msp"`
	LR         uint32 `json:"lr"`
	XPSR       uint32 `json:"xpsr"`
	PRIMASK    uint32 `json:"primask"`
	InstrCount uint64 `json:"instr_count"`
}

type PeripheralSnapshot struct {
	TIM2   TIM2Snapshot   `json:"tim2"`
	USART1 USART1Snapshot `json:"usart1"`
	NVIC   NVICSnapshot   `json:"nvic"`
}

type TIM2Snapshot struct {
	CR1  uint32 `json:"cr1"`
	PSC  uint32 `json:"psc"`
	ARR  uint32 `json:"arr"`
	CNT  uint32 `json:"cnt"`
	DIER uint32 `json:"dier"`
	SR   uint32 `json:"sr"`
}

type USART1Snapshot struct {
	SR  uint32 `json:"sr"`
	DR  uint32 `json:"dr"`
	BRR uint32 `json:"brr"`
	CR1 uint32 `json:"cr1"`
}

type NVICSnapshot struct {
	Selected int   `json:"selected"`
	Enabled  []int `json:"enabled"`
	Pending  []int `json:"pending"`
}

type PinSnapshot struct {
	Name  string `json:"name"`
	Port  string `json:"port"`
	Index int    `json:"index"`
	Mode  string `json:"mode"`
	Level *int   `json:"level"`
	Label string `json:"label,omitempty"`
}

const (
	SessionIdle      = "idle"
	SessionRunning   = "running"
	SessionStopped   = "stopped"
	SessionCompleted = "completed"
	SessionFailed    = "failed"
)

type SessionState struct {
	SessionID            string              `json:"session_id"`
	Status               string              `json:"status"`
	StopReason           string              `json:"stop_reason,omitempty"`
	UARTOutput           string              `json:"uart_output"`
	InstructionsExecuted uint64              `json:"instructions_executed"`
	CPU                  *CPUSnapshot        `json:"cpu,omitempty"`
	Peripherals          *PeripheralSnapshot `json:"peripherals,omitempty"`
	Pins                 []PinSnapshot       `json:"pins"`
	ErrorCode            string              `json:"error_code,omitempty"`
	Error                string              `json:"error,omitempty"`
}

type StepRequest struct {
	Steps uint64 `json:"steps"`
}

type RunRequest struct {
	MaxInstructions uint64 `json:"max_instructions"`
}

type PinControlRequest struct {
	Level *int   `json:"level"`
	Mode  string `json:"mode,omitempty"`
	Label string `json:"label,omitempty"`
}
