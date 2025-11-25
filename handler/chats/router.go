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

	// access: все
	chateRouter.Handle("/{chatId:[0-9]+}/settings", httpx.ErrorHandler(h.GetChatSettingsHandler)).Methods(http.MethodGet)

	// access: все
	chateRouter.Handle("/{chatId:[0-9]+}/blocked", middleware.RequireEmailConfirmed(
		httpx.ErrorHandler(h.ChatsUpdateBlockedHandler),
	)).Methods(http.MethodPatch)

	// access: все
	chateRouter.Handle("/{chatId:[0-9]+}/cbackground", middleware.RequireEmailConfirmed(
		httpx.ErrorHandler(h.ChatsUpdateCustomBackgroundHandler),
	)).Methods(http.MethodPost)
}
