package tests

import (
	"fmt"
	"net/http"
	"net/http/httptest"
	"net/url"
	"parmigiano/http/middleware"
	"testing"
)

func TestSecurityMiddleware_SQLInjectionBlocked_MultiplePlaces(t *testing.T) {
	handler := middleware.SecurityMiddleware()(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		w.WriteHeader(http.StatusOK)
	}))

	payloads := []struct {
		name    string
		payload string
		blocked bool
	}{
		{"DropTableSimple", "DROP TABLE users", true},
		{"DropTableSemicolon", "'; DROP TABLE users; --", true},
		{"ClassicOr1", "1 OR 1=1", true},
		// {"AuthBypassSingleQuote", "' OR '1'='1", true},
		{"UnionSelect", "UNION SELECT username, password FROM users", true},
		{"ExecCmd", "'; EXEC xp_cmdshell('dir'); --", true},
		{"SQLComment", "admin' --", true},
		{"Benign", "hello word", false},
		{"BenignText", "hello world", false},
		{"BenignNumbers", "1234567890", false},
		{"BenignEmail", "user@example.com", false},
		{"BenignJSON", `{"name":"Alice","age":30}`, false},
		{"BenignURL", "https://example.com/page?query=1", false},
		{"BenignFormData", "username=admin&password=12345", false},
	}

	sources := []struct {
		name    string
		makeReq func(payload, ip string) *http.Request
	}{
		{
			"body_raw",
			func(payload, ip string) *http.Request {
				req := MakeReqWithIp(http.MethodPost, "/", payload, ip)
				req.Header.Set("Content-Type", "text/plain")
				return req
			},
		},
		{
			"form_raw",
			func(payload, ip string) *http.Request {
				form := url.Values{}
				form.Set("q", payload)
				req := MakeReqWithIp(http.MethodPost, "/", form.Encode(), ip)
				req.Header.Set("Content-Type", "application/x-www-form-urlencoded")
				return req
			},
		},
		{
			"query_param",
			func(payload, ip string) *http.Request {
				escaped := url.QueryEscape(payload)
				req := MakeReqWithIp(http.MethodGet, "/?q="+escaped, "", ip)
				return req
			},
		},
		{
			"header_X-Query",
			func(payload, ip string) *http.Request {
				req := MakeReqWithIp(http.MethodGet, "/", "", ip)
				req.Header.Set("X-Query", payload)
				return req
			},
		},
		{
			"json_body",
			func(payload, ip string) *http.Request {
				jsonBody := fmt.Sprintf(`{"q":"%s"}`, payload)
				req := MakeReqWithIp(http.MethodPost, "/", jsonBody, ip)
				req.Header.Set("Content-Type", "application/json")
				return req
			},
		},
	}

	for pIdx, payload := range payloads {
		for sIdx, source := range sources {
			testName := payload.name + "/" + source.name
			ip := fmt.Sprintf("192.0.2.%d", 10+pIdx*10+sIdx)

			t.Run(testName, func(t *testing.T) {
				req := source.makeReq(payload.payload, ip)

				rec := httptest.NewRecorder()

				handler.ServeHTTP(rec, req)

				if payload.blocked {
					if rec.Code != http.StatusBadRequest {
						t.Errorf("payload %q from %s: expected status %d (blocked), got %d; body: %q",
							payload.payload, source.name, http.StatusBadRequest, rec.Code, rec.Body.String())
					}
				} else {
					if rec.Code != http.StatusOK {
						t.Errorf("payload %q from %s: expected status %d (ok), got %d; body: %q",
							payload.payload, source.name, http.StatusOK, rec.Code, rec.Body.String())
					}
				}
			})
		}
	}
}
