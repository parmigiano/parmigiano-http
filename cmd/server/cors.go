package main

import (
	"net/http"
	"os"

	"github.com/gorilla/handlers"
)

func (s *httpServer) cors(handler http.Handler) http.Handler {
	env := os.Getenv("GO_ENV")

	var origins []string
	if env == "DEV" {
		origins = []string{"http://localhost:5173"}
	} else {
		origins = []string{}
	}

	methods := handlers.AllowedMethods([]string{"GET", "POST", "PUT", "DELETE", "PATCH", "OPTIONS"})
	headers := handlers.AllowedHeaders([]string{"Content-Type", "X-Requested-With", "Authorization", "X-Captcha-Token"})
	exposed := handlers.ExposedHeaders([]string{""})

	allowCredentials := handlers.AllowCredentials()

	return handlers.CORS(handlers.AllowedOrigins(origins), methods, headers, exposed, allowCredentials)(handler)
}
