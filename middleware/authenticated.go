// nolint
package middleware

import (
	"context"
	"net/http"
	"parmigiano/http/handler"
	"parmigiano/http/infra/store/redis"
	"parmigiano/http/pkg/httpx"
	"parmigiano/http/types"
	"strings"

	"github.com/gorilla/mux"
)

// IsAuthenticatedMiddleware проверка на аутентифицированного пользователя
func IsAuthenticatedMiddleware(h *handler.BaseHandler) mux.MiddlewareFunc {
	return func(next http.Handler) http.Handler {
		return http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
			var sessionId string

			// from header
			authHeader := r.Header.Get("Authorization")
			if authHeader != "" {
				parts := strings.Split(authHeader, " ")
				if len(parts) == 2 && strings.ToLower(parts[0]) == "bearer" {
					sessionId = parts[1]
				}
			}

			// from cookie
			if sessionId == "" {
				cookie, err := r.Cookie("auth-token")
				if err == nil && cookie != nil {
					sessionId = cookie.Value
				}
			}

			if sessionId == "" {
				httpx.HttpResponse(w, r, http.StatusUnauthorized, "пожалуйста, подключитесь к учетной записи")
				return
			}

			session, err := redis.GetSession(sessionId)
			if err != nil {
				h.Logger.Error("%v", err)

				httpx.HttpResponse(w, r, http.StatusUnauthorized, "ошибка проверки токена, попробуйте позже")
				return
			}

			if session == nil {
				httpx.HttpResponse(w, r, http.StatusUnauthorized, "пожалуйста, подключитесь к учетной записи")
				return
			}

			// db: get user core
			user, err := h.Store.Users.Get_UserInfoByUserUid(r.Context(), session.UserUid)
			if err != nil {
				h.Logger.Error("%v", err)

				httpx.HttpResponse(w, r, http.StatusUnauthorized, "пожалуйста, подключитесь к учетной записи")
				return
			}

			if user == nil {
				httpx.HttpResponse(w, r, http.StatusUnauthorized, "пожалуйста, подключитесь к учетной записи")
				return
			}

			authTokenModel := &types.AuthToken{
				User: *user,
			}

			// session REFRESH
			redis.RefreshSession(sessionId)

			//nolint
			ctx := context.WithValue(r.Context(), "identity", authTokenModel)
			r = r.WithContext(ctx)

			next.ServeHTTP(w, r)
		})
	}
}

func GetIdentity(ctx context.Context) *types.AuthToken {
	if value, ok := ctx.Value("identity").(*types.AuthToken); ok {
		return value
	}

	return nil
}
