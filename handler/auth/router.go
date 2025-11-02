package auth

import (
	"net/http"
	"parmigiano/http/middleware"
	"parmigiano/http/pkg/httpx"

	"github.com/gorilla/mux"
)

func (h *Handler) RegisterRoutes(router *mux.Router) {
	authRouter := router.PathPrefix("/auth").Subrouter()

	authProtectedRouter := router.PathPrefix("/auth").Subrouter()
	authProtectedRouter.Use(middleware.IsAuthenticatedMiddleware(h.BaseHandler))

	// access: все
	authRouter.Handle("/create", httpx.ErrorHandler(h.AuthCreateUserHandler)).Methods(http.MethodPost)

	// access: все
	authRouter.Handle("/login", httpx.ErrorHandler(h.AuthLoginUserHandler)).Methods(http.MethodPost)

	// access: все
	authRouter.Handle("/confirm", httpx.ErrorHandler(h.AuthEmailConfirmHandler)).Methods(http.MethodGet)

	// access: все
	authProtectedRouter.Handle("/confirm/req", httpx.ErrorHandler(h.AuthEmailConfirmReqHandler)).Methods(http.MethodGet)

	// access: все
	authProtectedRouter.Handle("/delete", httpx.ErrorHandler(h.AuthDeleteHandler)).Methods(http.MethodDelete)
}
