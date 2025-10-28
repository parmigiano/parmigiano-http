package tests

import (
	"net/http"
	"net/http/httptest"
	"parmigiano/http/middleware"
	"strings"
	"testing"
	"time"
)

func TestSecurityMiddleware_BlockIPAfterViolations(t *testing.T) {
	middleware.Mutex.Lock()
	middleware.BlockedIPs = make(map[string]time.Time)
	middleware.IpViolations = make(map[string]int)
	middleware.Mutex.Unlock()

	handler := middleware.SecurityMiddleware()(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		w.WriteHeader(200)
	}))

	ip := "95.6.45.1"

	for i := 0; i < 3; i++ {
		req := httptest.NewRequest(http.MethodPost, "/", strings.NewReader("DROP TABLE users"))
		req.RemoteAddr = ip + ":1234"
		rec := httptest.NewRecorder()

		handler.ServeHTTP(rec, req)
	}

	req := httptest.NewRequest(http.MethodPost, "/", strings.NewReader("DROP TABLE users"))
	req.RemoteAddr = ip + ":1234"
	rec := httptest.NewRecorder()

	handler.ServeHTTP(rec, req)

	if rec.Code != http.StatusForbidden {
		t.Errorf("Expected status %d for blocked IP, got %d", http.StatusForbidden, rec.Code)
	}
}
