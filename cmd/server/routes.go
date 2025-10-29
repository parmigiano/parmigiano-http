package main

import (
	"net/http"
	"parmigiano/http/handler"
	"parmigiano/http/handler/auth"
	"parmigiano/http/handler/meta"
	"parmigiano/http/middleware"

	"github.com/gorilla/mux"
)

func (s *httpServer) routes() http.Handler {
	router := mux.NewRouter()

	// middleware for logging API request
	router.Use(middleware.NewLogger(s.logger).LoggerMiddleware)
	// middleware for get exception errors
	router.Use(middleware.RecoveryMiddleware())
	// middleware for security API
	router.Use(middleware.SecurityMiddleware())
	// middleware rate limiter
	router.Use(middleware.RateLimiterMiddleware(10, 20))

	subrouter := router.PathPrefix("/api/v1").Subrouter()

	baseHandler := &handler.BaseHandler{
		Db:     s.db,
		Logger: s.logger,
		Store:  s.store,
	}

	// routes path
	// authenticate
	auth.NewHandler(baseHandler).RegisterRoutes(subrouter)
	// meta
	meta.NewHandler(baseHandler).RegisterRoutes(subrouter)

	return s.cors(router)
}
