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
	userRouter.Handle("/me", httpx.ErrorHandler(h.GetUserMeHandler)).Methods(http.MethodGet)

	// access: все
	userRouter.Handle("/me", httpx.ErrorHandler(h.UserUpdateProfile)).Methods(http.MethodPatch)

	// access: все
	userRouter.Handle("/upload/avatar", middleware.RequireEmailConfirmed(
		httpx.ErrorHandler(h.UserUpdateAvatarHandler),
	)).Methods(http.MethodPost)

	// access: все
	userRouter.Handle("/find/{username}", httpx.ErrorHandler(h.UsersFindByUsernameHandler)).Methods(http.MethodGet)
}
