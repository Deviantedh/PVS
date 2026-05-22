package httpapi

import (
	"context"
	"encoding/json"
	"errors"
	"fmt"
	"log/slog"
	"net/http"
	"strings"

	"github.com/Deviantedh/PVS/service/internal/model"
	"github.com/Deviantedh/PVS/service/internal/session"
)

type Runner interface {
	Run(ctx context.Context, job model.Job) (model.Result, error)
}

type Server struct {
	runner   Runner
	sessions *session.Manager
	logger   *slog.Logger
}

func NewServer(runner Runner, logger *slog.Logger) *Server {
	if logger == nil {
		logger = slog.Default()
	}
	return &Server{
		runner:   runner,
		sessions: session.NewManager(runner),
		logger:   logger,
	}
}

func (s *Server) Handler() http.Handler {
	mux := http.NewServeMux()
	mux.HandleFunc("/api/run", s.handleRun)
	mux.HandleFunc("/api/session", s.handleCreateSession)
	mux.HandleFunc("/api/session/", s.handleSession)
	return withCORS(mux)
}

func (s *Server) handleRun(w http.ResponseWriter, r *http.Request) {
	if r.Method == http.MethodOptions {
		w.WriteHeader(http.StatusNoContent)
		return
	}
	if r.Method != http.MethodPost {
		w.Header().Set("Allow", "POST, OPTIONS")
		writeJSON(w, http.StatusMethodNotAllowed, model.Result{
			Status:    model.StatusBadInput,
			ExitCode:  13,
			ErrorCode: model.ErrorInvalidJob,
			Error:     "method not allowed",
		})
		return
	}

	var job model.Job
	decoder := json.NewDecoder(r.Body)
	decoder.DisallowUnknownFields()
	if err := decoder.Decode(&job); err != nil {
		writeJSON(w, http.StatusBadRequest, model.Result{
			Status:    model.StatusBadInput,
			ExitCode:  13,
			ErrorCode: model.ErrorDecodeJob,
			Error:     "decode job: " + err.Error(),
		})
		return
	}
	if err := validateJob(job); err != nil {
		writeJSON(w, http.StatusBadRequest, model.Result{
			JobID:     job.JobID,
			Status:    model.StatusBadInput,
			ExitCode:  13,
			ErrorCode: model.ErrorInvalidJob,
			Error:     err.Error(),
		})
		return
	}

	result, err := s.runner.Run(r.Context(), job)
	if err != nil {
		s.logger.Error("runner failed", "job_id", job.JobID, "error", err)
		result = model.Result{
			JobID:     job.JobID,
			Status:    model.StatusCrash,
			ExitCode:  12,
			ErrorCode: model.ErrorSimulatorCrash,
			Error:     err.Error(),
		}
	}
	writeJSON(w, statusCodeForResult(result), result)
}

func (s *Server) handleCreateSession(w http.ResponseWriter, r *http.Request) {
	if r.Method == http.MethodOptions {
		w.WriteHeader(http.StatusNoContent)
		return
	}
	if r.Method != http.MethodPost {
		w.Header().Set("Allow", "POST, OPTIONS")
		writeJSON(w, http.StatusMethodNotAllowed, errorState("", "method not allowed", model.ErrorInvalidJob))
		return
	}

	job, ok := decodeJobRequest(w, r)
	if !ok {
		return
	}

	state, err := s.sessions.Create(job)
	if err != nil {
		writeJSON(w, http.StatusInternalServerError, errorState("", err.Error(), model.ErrorSimulatorCrash))
		return
	}
	writeJSON(w, http.StatusCreated, state)
}

func (s *Server) handleSession(w http.ResponseWriter, r *http.Request) {
	if r.Method == http.MethodOptions {
		w.WriteHeader(http.StatusNoContent)
		return
	}

	parts := strings.Split(strings.TrimPrefix(r.URL.Path, "/api/session/"), "/")
	if len(parts) == 0 || parts[0] == "" {
		writeJSON(w, http.StatusNotFound, errorState("", "session id is required", model.ErrorInvalidJob))
		return
	}
	id := parts[0]

	if len(parts) == 1 {
		if r.Method != http.MethodGet {
			w.Header().Set("Allow", "GET, OPTIONS")
			writeJSON(w, http.StatusMethodNotAllowed, errorState(id, "method not allowed", model.ErrorInvalidJob))
			return
		}
		s.writeSessionState(w, id)
		return
	}

	switch parts[1] {
	case "step":
		s.handleSessionStep(w, r, id)
	case "run":
		s.handleSessionRun(w, r, id)
	case "stop":
		s.handleSessionStop(w, r, id)
	case "events":
		s.handleSessionEvents(w, r, id)
	case "pins":
		if len(parts) != 3 {
			writeJSON(w, http.StatusNotFound, errorState(id, "pin name is required", model.ErrorInvalidJob))
			return
		}
		s.handleSessionPin(w, r, id, parts[2])
	default:
		writeJSON(w, http.StatusNotFound, errorState(id, "unknown session endpoint", model.ErrorInvalidJob))
	}
}

func (s *Server) writeSessionState(w http.ResponseWriter, id string) {
	state, err := s.sessions.Get(id)
	if err != nil {
		writeSessionError(w, id, err)
		return
	}
	writeJSON(w, http.StatusOK, state)
}

func (s *Server) handleSessionStep(w http.ResponseWriter, r *http.Request, id string) {
	if r.Method != http.MethodPost {
		w.Header().Set("Allow", "POST, OPTIONS")
		writeJSON(w, http.StatusMethodNotAllowed, errorState(id, "method not allowed", model.ErrorInvalidJob))
		return
	}

	var request model.StepRequest
	if !decodeOptionalJSON(w, r, &request) {
		return
	}
	state, err := s.sessions.Step(r.Context(), id, request.Steps)
	if err != nil {
		writeSessionError(w, id, err)
		return
	}
	writeJSON(w, http.StatusOK, state)
}

func (s *Server) handleSessionRun(w http.ResponseWriter, r *http.Request, id string) {
	if r.Method != http.MethodPost {
		w.Header().Set("Allow", "POST, OPTIONS")
		writeJSON(w, http.StatusMethodNotAllowed, errorState(id, "method not allowed", model.ErrorInvalidJob))
		return
	}

	var request model.RunRequest
	if !decodeOptionalJSON(w, r, &request) {
		return
	}
	state, err := s.sessions.Run(r.Context(), id, request.MaxInstructions)
	if err != nil {
		writeSessionError(w, id, err)
		return
	}
	writeJSON(w, http.StatusOK, state)
}

func (s *Server) handleSessionStop(w http.ResponseWriter, r *http.Request, id string) {
	if r.Method != http.MethodPost {
		w.Header().Set("Allow", "POST, OPTIONS")
		writeJSON(w, http.StatusMethodNotAllowed, errorState(id, "method not allowed", model.ErrorInvalidJob))
		return
	}
	state, err := s.sessions.Stop(id)
	if err != nil {
		writeSessionError(w, id, err)
		return
	}
	writeJSON(w, http.StatusOK, state)
}

func (s *Server) handleSessionPin(w http.ResponseWriter, r *http.Request, id string, name string) {
	if r.Method != http.MethodPost {
		w.Header().Set("Allow", "POST, OPTIONS")
		writeJSON(w, http.StatusMethodNotAllowed, errorState(id, "method not allowed", model.ErrorInvalidJob))
		return
	}

	var request model.PinControlRequest
	decoder := json.NewDecoder(r.Body)
	decoder.DisallowUnknownFields()
	if err := decoder.Decode(&request); err != nil {
		writeJSON(w, http.StatusBadRequest, errorState(id, "decode pin request: "+err.Error(), model.ErrorDecodeJob))
		return
	}
	if request.Level != nil && (*request.Level < 0 || *request.Level > 1) {
		writeJSON(w, http.StatusBadRequest, errorState(id, "pin level must be 0, 1, or null", model.ErrorInvalidJob))
		return
	}

	state, err := s.sessions.SetPin(id, name, request)
	if err != nil {
		writeSessionError(w, id, err)
		return
	}
	writeJSON(w, http.StatusOK, state)
}

func (s *Server) handleSessionEvents(w http.ResponseWriter, r *http.Request, id string) {
	if r.Method != http.MethodGet {
		w.Header().Set("Allow", "GET, OPTIONS")
		writeJSON(w, http.StatusMethodNotAllowed, errorState(id, "method not allowed", model.ErrorInvalidJob))
		return
	}

	ch, cancel, err := s.sessions.Subscribe(id)
	if err != nil {
		writeSessionError(w, id, err)
		return
	}
	defer cancel()

	w.Header().Set("Content-Type", "text/event-stream")
	w.Header().Set("Cache-Control", "no-cache")
	w.Header().Set("Connection", "keep-alive")
	flusher, _ := w.(http.Flusher)

	for {
		select {
		case <-r.Context().Done():
			return
		case state, ok := <-ch:
			if !ok {
				return
			}
			data, err := json.Marshal(state)
			if err != nil {
				s.logger.Error("encode session event failed", "session_id", id, "error", err)
				return
			}
			fmt.Fprintf(w, "event: snapshot\ndata: %s\n\n", data)
			if flusher != nil {
				flusher.Flush()
			}
		}
	}
}

func validateJob(job model.Job) error {
	if job.JobID == "" {
		return errors.New("job_id is required")
	}
	if job.Firmware == "" {
		return errors.New("firmware is required")
	}
	return nil
}

func statusCodeForResult(result model.Result) int {
	switch result.Status {
	case model.StatusOK:
		return http.StatusOK
	case model.StatusBadInput:
		return http.StatusBadRequest
	case model.StatusTimeout:
		return http.StatusGatewayTimeout
	case model.StatusFault, model.StatusUnsupportedInstr:
		return http.StatusUnprocessableEntity
	default:
		return http.StatusBadGateway
	}
}

func decodeJobRequest(w http.ResponseWriter, r *http.Request) (model.Job, bool) {
	var job model.Job
	decoder := json.NewDecoder(r.Body)
	decoder.DisallowUnknownFields()
	if err := decoder.Decode(&job); err != nil {
		writeJSON(w, http.StatusBadRequest, errorState("", "decode job: "+err.Error(), model.ErrorDecodeJob))
		return model.Job{}, false
	}
	if err := validateJob(job); err != nil {
		writeJSON(w, http.StatusBadRequest, errorState(job.JobID, err.Error(), model.ErrorInvalidJob))
		return model.Job{}, false
	}
	return job, true
}

func decodeOptionalJSON(w http.ResponseWriter, r *http.Request, value any) bool {
	if r.Body == nil || r.ContentLength == 0 {
		return true
	}
	decoder := json.NewDecoder(r.Body)
	decoder.DisallowUnknownFields()
	if err := decoder.Decode(value); err != nil {
		writeJSON(w, http.StatusBadRequest, errorState("", "decode request: "+err.Error(), model.ErrorDecodeJob))
		return false
	}
	return true
}

func writeSessionError(w http.ResponseWriter, id string, err error) {
	if errors.Is(err, session.ErrNotFound) {
		writeJSON(w, http.StatusNotFound, errorState(id, err.Error(), model.ErrorInvalidJob))
		return
	}
	writeJSON(w, http.StatusInternalServerError, errorState(id, err.Error(), model.ErrorSimulatorCrash))
}

func errorState(id string, message string, code string) model.SessionState {
	return model.SessionState{
		SessionID:  id,
		Status:     model.SessionFailed,
		ErrorCode:  code,
		Error:      message,
		Pins:       []model.PinSnapshot{},
		UARTOutput: "",
	}
}

func writeJSON(w http.ResponseWriter, status int, value any) {
	w.Header().Set("Content-Type", "application/json")
	w.WriteHeader(status)
	_ = json.NewEncoder(w).Encode(value)
}

func withCORS(next http.Handler) http.Handler {
	return http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		w.Header().Set("Access-Control-Allow-Origin", "*")
		w.Header().Set("Access-Control-Allow-Methods", "GET, POST, OPTIONS")
		w.Header().Set("Access-Control-Allow-Headers", "Content-Type")
		next.ServeHTTP(w, r)
	})
}
