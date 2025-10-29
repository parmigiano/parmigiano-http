package meta

import (
	"net/http"
	"parmigiano/http/pkg/httpx"

	"github.com/gorilla/mux"
)

func (h *Handler) RegisterRoutes(router *mux.Router) {
	metaRouter := router.PathPrefix("/meta").Subrouter()

	// access: все
	metaRouter.Handle("/ack", httpx.ErrorHandler(h.MetaAck)).Methods(http.MethodGet)
}
