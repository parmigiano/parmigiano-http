package messages

import (
	"parmigiano/http/middleware"

	"github.com/gorilla/mux"
)

func (h *Handler) RegisterRoutes(router *mux.Router) {
	messageRouter := router.PathPrefix("/messages").Subrouter()
	messageRouter.Use(middleware.IsAuthenticatedMiddleware(h.BaseHandler))

	// access: все
}
