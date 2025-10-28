package httpx

import (
	"compress/gzip"
	"crypto/md5"
	"encoding/hex"
	"errors"
	"fmt"
	"io"
	"net/http"
	"parmigiano/http/config"
	"parmigiano/http/pkg/httpx/httperr"
	"strconv"
	"strings"
)

func HttpParse(r *http.Request, payload any) error {
	if r.Body == nil {
		return errors.New("request text is missing")
	}

	return config.JSON.NewDecoder(r.Body).Decode(payload)
}

func HttpResponse(w http.ResponseWriter, r *http.Request, status int, v any) {
	accept := r.Header.Get("Accept-Encoding")
	shouldGzip := strings.Contains(accept, "gzip")

	var writer io.Writer = w
	var gzipWriter *gzip.Writer

	if shouldGzip {
		w.Header().Set("Content-Encoding", "gzip")
		w.Header().Set("Vary", "Accept-Encoding")

		gzipWriter = gzip.NewWriter(w)
		defer gzipWriter.Close()

		writer = gzipWriter
	}

	w.Header().Set("Content-Security-Policy", "script-src 'self';")
	w.Header().Set("Content-Type", "application/json")
	w.WriteHeader(status)

	if err := config.JSON.NewEncoder(writer).Encode(map[string]any{"message": v}); err != nil {
		http.Error(w, "unknown server error", http.StatusInternalServerError)
	}
}

func HttpFileResponse(w http.ResponseWriter, r *http.Request, filename string, data []byte, contentType string) {
	if len(data) == 0 {
		http.Error(w, "unknown server error", http.StatusInternalServerError)
		return
	}

	w.Header().Set("Content-Disposition", fmt.Sprintf("attachment; filename=%q", filename))
	if contentType == "" {
		contentType = "application/octet-stream"
	}
	w.Header().Set("Content-Type", contentType)
	w.Header().Set("Content-Length", strconv.Itoa(len(data)))

	_, _ = w.Write(data)
}

func HttpResponseWithETag(w http.ResponseWriter, r *http.Request, status int, data any) {
	jsonBytes, err := config.JSON.Marshal(data)
	if err != nil {
		http.Error(w, "unknown server error", http.StatusInternalServerError)
		return
	}

	hash := md5.Sum(jsonBytes)
	etag := `"` + hex.EncodeToString(hash[:]) + `"`

	if match := r.Header.Get("If-None-Match"); match == etag {
		w.Header().Set("ETag", etag)
		w.WriteHeader(http.StatusNotModified)
		return
	}

	w.Header().Set("ETag", etag)
	HttpResponse(w, r, status, data)
}

func HttpCache(w http.ResponseWriter, limit int) {
	w.Header().Set("Cache-Control", fmt.Sprintf("public, max-age=%d", limit))
}

func HttpResponseError(w http.ResponseWriter, r *http.Request, err error) {
	if httpErr, ok := err.(httperr.HTTPError); ok {
		HttpResponse(w, r, httpErr.StatusCode(), httpErr.Error())
		return
	}

	HttpResponse(w, r, http.StatusInternalServerError, err.Error())
}
