package main

import (
	"context"
	"flag"
	"log/slog"
	"net/http"
	"os"
	"os/signal"
	"syscall"
	"time"

	"github.com/Deviantedh/PVS/service/internal/httpapi"
	"github.com/Deviantedh/PVS/service/internal/runner"
)

func main() {
	var addr string
	var simulatorPath string
	var workDir string

	flag.StringVar(&addr, "addr", "127.0.0.1:8080", "HTTP listen address")
	flag.StringVar(&simulatorPath, "simulator", "", "path to simulator executable")
	flag.StringVar(&workDir, "workdir", "", "working directory for simulator subprocess")
	flag.Parse()

	logger := slog.New(slog.NewTextHandler(os.Stderr, nil))
	simRunner := runner.New(runner.Config{
		SimulatorPath: simulatorPath,
		WorkDir:       workDir,
	})
	api := httpapi.NewServer(simRunner, logger)

	server := &http.Server{
		Addr:              addr,
		Handler:           api.Handler(),
		ReadHeaderTimeout: 5 * time.Second,
	}

	ctx, stop := signal.NotifyContext(context.Background(), os.Interrupt, syscall.SIGTERM)
	defer stop()

	go func() {
		logger.Info("pvs HTTP server listening", "addr", addr)
		if err := server.ListenAndServe(); err != nil && err != http.ErrServerClosed {
			logger.Error("HTTP server failed", "error", err)
			stop()
		}
	}()

	<-ctx.Done()

	shutdownCtx, cancel := context.WithTimeout(context.Background(), 5*time.Second)
	defer cancel()
	if err := server.Shutdown(shutdownCtx); err != nil {
		logger.Error("HTTP server shutdown failed", "error", err)
		os.Exit(1)
	}
}
