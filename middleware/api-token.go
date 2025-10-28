package middleware

import (
	"net/http"
	"os"
	"parmigiano/http/pkg/httpx"

	"github.com/gorilla/mux"
)

func ApiTokenMiddleware() mux.MiddlewareFunc {
	return func(next http.Handler) http.Handler {
		return http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
			token := r.Header.Get("X-App-Key")

			if token != os.Getenv("APPKEY_TOKEN") {
				httpx.HttpResponse(w, r, http.StatusForbidden, "неавторизованный клиент")
				return
			}

			next.ServeHTTP(w, r)
		})
	}
}
