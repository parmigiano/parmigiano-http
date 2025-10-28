package middleware

import (
	"errors"
	"fmt"
	"net/http"
	"parmigiano/http/pkg/httpx"

	"github.com/gorilla/mux"
)

// RecoveryMiddleware middleware для перехвата паник (panic) во время обработки HTTP-запросов
func RecoveryMiddleware() mux.MiddlewareFunc {
	return func(next http.Handler) http.Handler {
		return http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
			defer func() {
				if rec := recover(); rec != nil {
					fmt.Println(rec)
					httpx.HttpResponseError(w, r, errors.New("упс, что-то пошло не так"))
				}
			}()

			next.ServeHTTP(w, r)
		})
	}
}
