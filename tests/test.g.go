package tests

import (
	"net/http"
	"net/http/httptest"
	"strings"
)

func MakeReqWithIp(method, target, body, ip string) *http.Request {
	req := httptest.NewRequest(method, target, strings.NewReader(body))
	req.RemoteAddr = ip + ":12345"
	return req
}
