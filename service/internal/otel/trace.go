package otel

import (
	"context"
	"log/slog"
	"time"
)

type Logger struct {
	logger *slog.Logger
}

func New(logger *slog.Logger) Logger {
	if logger == nil {
		logger = slog.Default()
	}
	return Logger{logger: logger}
}

func (l Logger) Span(ctx context.Context, name string, attrs ...any) func(status string) {
	start := time.Now()
	l.logger.InfoContext(ctx, name+".start", attrs...)

	return func(status string) {
		values := append([]any{}, attrs...)
		values = append(values, "status", status, "duration_ms", time.Since(start).Milliseconds())
		l.logger.InfoContext(ctx, name+".end", values...)
	}
}
