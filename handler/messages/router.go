package messages

import (
	"net/http"
	"parmigiano/http/middleware"
	"parmigiano/http/pkg/httpx"

	"github.com/gorilla/mux"
)

func (h *Handler) RegisterRoutes(router *mux.Router) {
	messageRouter := router.PathPrefix("/messages").Subrouter()
	messageRouter.Use(middleware.IsAuthenticatedMiddleware(h.BaseHandler))

	// access: все
	messageRouter.Handle("/history/{senderUid:[0-9]+}", middleware.RequireEmailConfirmed(
		httpx.ErrorHandler(h.MessagesGetHistoryHandler),
	)).Methods(http.MethodGet)
}
