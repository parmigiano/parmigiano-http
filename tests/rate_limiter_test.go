package tests

import (
	"net/http"
	"net/http/httptest"
	"parmigiano/http/middleware"
	"testing"
	"time"

	"github.com/gorilla/mux"
)

func TestRateLimiterMiddleware(t *testing.T) {
	rps := 10.0
	burst := 5
	mw := middleware.RateLimiterMiddleware(rps, burst)

	handler := mw(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		w.WriteHeader(http.StatusOK)
	}))

	router := mux.NewRouter()
	router.Handle("/", handler)

	for i := 0; i < 5; i++ {
		req := httptest.NewRequest("GET", "/", nil)
		w := httptest.NewRecorder()

		router.ServeHTTP(w, req)

		if i < burst+int(rps) {
			if w.Result().StatusCode != http.StatusOK {
				t.Errorf("request %d: expected 200 OK, got %d", i+1, w.Result().StatusCode)
			}
		} else {
			if w.Result().StatusCode != http.StatusTooManyRequests {
				t.Errorf("request %d: expected 429 Too Many Requests, got %d", i+1, w.Result().StatusCode)
			}
		}

		time.Sleep(500 * time.Millisecond)
	}
}
