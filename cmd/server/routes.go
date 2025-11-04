package main

import (
	"net/http"
	"parmigiano/http/handler"
	"parmigiano/http/handler/auth"
	"parmigiano/http/handler/messages"
	"parmigiano/http/handler/meta"
	"parmigiano/http/handler/users"
	"parmigiano/http/handler/wsocket"
	"parmigiano/http/middleware"

	"github.com/gorilla/mux"
)

func (s *httpServer) routes() http.Handler {
	router := mux.NewRouter()

	apirouter := router.PathPrefix("/api/v1").Subrouter()

	// middleware for logging API request
	apirouter.Use(middleware.NewLogger(s.logger).LoggerMiddleware)
	// middleware for get exception errors
	apirouter.Use(middleware.RecoveryMiddleware())
	// middleware for security API
	apirouter.Use(middleware.SecurityMiddleware())
	// middleware rate limiter
	apirouter.Use(middleware.RateLimiterMiddleware(10, 20))

	baseHandler := &handler.BaseHandler{
		Db:     s.db,
		Logger: s.logger,
		Store:  s.store,
	}

	// routes path
	// authenticate
	auth.NewHandler(baseHandler).RegisterRoutes(apirouter)
	// users
	users.NewHandler(baseHandler).RegisterRoutes(apirouter)
	// messages
	messages.NewHandler(baseHandler).RegisterRoutes(apirouter)
	// meta
	meta.NewHandler(baseHandler).RegisterRoutes(apirouter)

	router.PathPrefix("/api/").Handler(s.cors(apirouter))

	// websocket connection client (pkg. wsocket)
	router.HandleFunc("/wsocket", wsocket.HandleWebSocket)

	return router
}
