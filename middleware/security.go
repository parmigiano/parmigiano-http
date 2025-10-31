package middleware

import (
	"io"
	"net"
	"net/http"
	"parmigiano/http/infra/logger"
	"parmigiano/http/pkg/httpx"
	"regexp"
	"strings"
	"sync"
	"time"

	"github.com/gorilla/mux"
)

const (
	MaxBodySize        = 5 << 20
	MaxViolationsPerIP = 3
	IpBlockDuration    = 7 * time.Minute
)

var (
	BlockedIPs   = make(map[string]time.Time)
	IpViolations = make(map[string]int)
	Mutex        sync.Mutex
)

// --- сигнатуры SQL-инъекций ---
var sqlInjectionPatterns = []*regexp.Regexp{
	regexp.MustCompile(`(?i)\bDROP\s+TABLE\b`),
	regexp.MustCompile(`(?i)\bUNION\s+SELECT\b`),
	regexp.MustCompile(`(?i)\bSELECT\b.+\bFROM\b`),
	regexp.MustCompile(`(?i)\bINSERT\s+INTO\b`),
	regexp.MustCompile(`(?i)\bUPDATE\s+\w+\s+SET\b`),
	regexp.MustCompile(`(?i)\bDELETE\s+FROM\b`),

	// классика
	regexp.MustCompile(`(?i)'\s*OR\s*'1'\s*=\s*'1'`),
	regexp.MustCompile(`(?i)\bOR\s+1\s*=\s*1\b`),
	regexp.MustCompile(`(?i)(?:--|#)`),
	regexp.MustCompile(`(?i);\s*exec\s+xp_cmdshell`),
}

// --- сигнатуры XSS-атак ---
var xssPattern = regexp.MustCompile(`(?i)(<script.*?>.*?</script>|<.*?on\w+\s*=\s*['"]?.*?['"]?|javascript:)`)

// SecurityMiddleware middleware проверок данных от пользователей (XSS, SQLInjection, ...)
func SecurityMiddleware() mux.MiddlewareFunc { //nolint
	return func(next http.Handler) http.Handler {
		return http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
			log := logger.NewLogger()
			ip := getIP(r)

			log.Ip(ip)

			if isBlocked(ip) {
				log.Warning("[ACCESS] Blocked IP %s", ip)
				httpx.HttpResponse(w, r, http.StatusForbidden, "access denied")
				return
			}

			contentType := r.Header.Get("Content-Type")

			if strings.HasPrefix(contentType, "multipart/form-data") {
				next.ServeHTTP(w, r)
				return
			}

			if strings.HasPrefix(contentType, "application/octet-stream") {
				next.ServeHTTP(w, r)
				return
			}

			// limit size
			r.Body = http.MaxBytesReader(w, r.Body, MaxBodySize)

			var bodyContent string
			if r.Method == http.MethodPost || r.Method == http.MethodPut {
				data, err := io.ReadAll(r.Body)
				if err != nil {
					httpx.HttpResponse(w, r, http.StatusRequestEntityTooLarge, "текст запроса слишком большой")
					registerViolation(ip)
					return
				}

				bodyContent = string(data)
				r.Body = io.NopCloser(strings.NewReader(bodyContent))
			}

			// check query params
			for _, values := range r.URL.Query() {
				for _, val := range values {
					if isMalicious(val) {
						httpx.HttpResponse(w, r, http.StatusBadRequest, "вредоносный контент")
						registerViolation(ip)
						return
					}
				}
			}

			// checkb form
			_ = r.ParseForm()
			for _, values := range r.Form {
				for _, val := range values {
					if isMalicious(val) {
						httpx.HttpResponse(w, r, http.StatusBadRequest, "вредоносный контент")
						registerViolation(ip)
						return
					}
				}
			}

			// check body
			if bodyContent != "" && isMalicious(bodyContent) {
				httpx.HttpResponse(w, r, http.StatusBadRequest, "вредоносный контент в теле")
				registerViolation(ip)
				return
			}

			// check headers
			for key, values := range r.Header {
				if strings.EqualFold(key, "Authorization") ||
					strings.EqualFold(key, "User-Agent") ||
					strings.EqualFold(key, "Accept") {
					continue
				}

				for _, val := range values {
					if isMalicious(val) {
						httpx.HttpResponse(w, r, http.StatusBadRequest, "вредоносный контент")
						registerViolation(ip)
						return
					}
				}
			}

			// check cookies
			for _, cookie := range r.Cookies() {
				if isMalicious(cookie.Value) {
					httpx.HttpResponse(w, r, http.StatusBadRequest, "вредоносный контент")
					registerViolation(ip)
					return
				}
			}

			next.ServeHTTP(w, r)
		})
	}
}

func isMalicious(input string) bool {
	for _, re := range sqlInjectionPatterns {
		if re.MatchString(input) {
			return true
		}
	}

	return xssPattern.MatchString(input)
}

func getIP(r *http.Request) string {
	// CF-Connecting-IP (Cloudflare)
	if cf := strings.TrimSpace(r.Header.Get("CF-Connecting-IP")); cf != "" {
		if ip := net.ParseIP(cf); ip != nil {
			return cf
		}
	}

	// X-Forwarded-For
	if xff := r.Header.Get("X-Forwarded-For"); xff != "" {
		parts := strings.Split(xff, ",")

		for _, p := range parts {
			ipStr := strings.TrimSpace(p)
			if ip := net.ParseIP(ipStr); ip != nil && !isInternalIP(ip) {
				return ipStr
			}
		}

		// если публичных не найдено — первый
		first := strings.TrimSpace(parts[0])
		if net.ParseIP(first) != nil {
			return first
		}
	}

	// X-Real-IP (nginx)
	if xr := strings.TrimSpace(r.Header.Get("X-Real-IP")); xr != "" {
		if net.ParseIP(xr) != nil {
			return xr
		}
	}

	host, _, err := net.SplitHostPort(r.RemoteAddr)
	if err != nil {
		return r.RemoteAddr
	}

	return host
}

func isInternalIP(ip net.IP) bool {
	if ip.IsLoopback() || ip.IsLinkLocalUnicast() || ip.IsLinkLocalMulticast() {
		return true
	}

	privateCIDRs := []string{
		"127.0.0.0/8",
		"10.0.0.0/8",
		"172.16.0.0/12",
		"192.168.0.0/16",
		"::1/128",
		"fc00::/7",
		"fe80::/10",
	}

	for _, cidr := range privateCIDRs {
		_, block, _ := net.ParseCIDR(cidr)
		if block.Contains(ip) {
			return true
		}
	}

	return false
}

func isBlocked(ip string) bool {
	if isInternalIP(net.ParseIP(ip)) {
		return false
	}

	Mutex.Lock()
	defer Mutex.Unlock()

	expiry, exists := BlockedIPs[ip]
	if !exists {
		return false
	}

	if time.Now().After(expiry) {
		delete(BlockedIPs, ip)
		delete(IpViolations, ip)

		return false
	}

	return true
}

func registerViolation(ip string) {
	Mutex.Lock()
	defer Mutex.Unlock()

	IpViolations[ip]++
	if IpViolations[ip] >= MaxViolationsPerIP {
		BlockedIPs[ip] = time.Now().Add(IpBlockDuration)
	}
}
