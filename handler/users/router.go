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
	// получение профиля пользователя
	userRouter.Handle("/me", httpx.ErrorHandler(h.GetUserMeHandler)).Methods(http.MethodGet)

	// access: все
	// обновление профиля пользователя
	userRouter.Handle("/me", httpx.ErrorHandler(h.UserUpdateProfile)).Methods(http.MethodPatch)

	// access: все
	// обновление аватара пользователя
	userRouter.Handle("/me/avatar", middleware.RequireEmailConfirmed(
		httpx.ErrorHandler(h.UserUpdateAvatarHandler),
	)).Methods(http.MethodPost)

	// access: все
	// получение профиля другого пользователя
	userRouter.Handle("/{uid}", httpx.ErrorHandler(h.GetUserProfileHandler)).Methods(http.MethodGet)
}
