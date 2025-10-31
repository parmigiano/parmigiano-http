package tests

import (
	"bytes"
	"net/http"
	"net/http/httptest"
	"parmigiano/http/middleware"
	"testing"
)

func TestSecurityMiddleware_BodyTooLarge(t *testing.T) {
	handler := middleware.SecurityMiddleware()(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		w.WriteHeader(200)
	}))

	largeBody := bytes.Repeat([]byte("A"), 1024*1024+7)
	req := httptest.NewRequest(http.MethodPost, "/", bytes.NewReader(largeBody))
	rec := httptest.NewRecorder()

	handler.ServeHTTP(rec, req)

	if rec.Code != http.StatusRequestEntityTooLarge {
		t.Errorf("Expected status %d, got %d", http.StatusRequestEntityTooLarge, rec.Code)
	}
}
