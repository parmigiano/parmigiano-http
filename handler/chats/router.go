package chats

import (
	"net/http"
	"parmigiano/http/middleware"
	"parmigiano/http/pkg/httpx"

	"github.com/gorilla/mux"
)

func (h *Handler) RegisterRoutes(router *mux.Router) {
	chateRouter := router.PathPrefix("/chats").Subrouter()
	chateRouter.Use(middleware.IsAuthenticatedMiddleware(h.BaseHandler))

	// access: все
	chateRouter.Handle("", httpx.ErrorHandler(h.GetChatsHandler)).Methods(http.MethodGet)

	// access: все
	chateRouter.Handle("/history/{senderUid:[0-9]+}", middleware.RequireEmailConfirmed(
		httpx.ErrorHandler(h.ChatsGetHistoryHandler),
	)).Methods(http.MethodGet)
}
