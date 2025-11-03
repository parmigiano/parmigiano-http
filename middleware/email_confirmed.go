package middleware

import (
	"net/http"
	"parmigiano/http/pkg/httpx"
)

func RequireEmailConfirmed(next http.Handler) http.Handler {
	return http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		authToken := GetIdentity(r.Context())

		if authToken == nil || !authToken.User.EmailConfirmed {
			httpx.HttpResponse(w, r, http.StatusForbidden, "пожалуйста, подтвердите свой email")
			return
		}

		next.ServeHTTP(w, r)
	})
}
