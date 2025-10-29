package meta

import (
	"net/http"
	"parmigiano/http/pkg/httpx"
	"time"
)

func (h *Handler) MetaAck(w http.ResponseWriter, r *http.Request) error {
	httpx.HttpResponse(w, r, http.StatusOK, time.Now().Unix())
	return nil
}
