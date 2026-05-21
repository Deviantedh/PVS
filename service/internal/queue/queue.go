package queue

import (
	"context"

	"github.com/Deviantedh/PVS/service/internal/model"
)

type Queue interface {
	Receive(ctx context.Context) (model.Job, error)
	Publish(ctx context.Context, result model.Result) error
}
