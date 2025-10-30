package users

import (
	"net/http"
	"parmigiano/http/middleware"
	"parmigiano/http/pkg/httpx"

	"github.com/gorilla/mux"
)

func (h *Handler) RegisterRoutes(router *mux.Router) {
	userRouter := router.PathPrefix("/users").Subrouter()
	userRouter.Use(middleware.IsAuthenticatedMiddleware(h.BaseHandler))

	// access: все
	userRouter.Handle("/me", httpx.ErrorHandler(h.GetUserMe)).Methods(http.MethodGet)
}
