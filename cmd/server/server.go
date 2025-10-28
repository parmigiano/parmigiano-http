package main

import (
	"fmt"
	"log"
	"net/http"
	"parmigiano/http/config"
	"parmigiano/http/infra/constants"
	"strconv"
)

func (s *httpServer) httpStart() error {
	port, err := strconv.Atoi(config.HttpServerPort)
	if err != nil {
		log.Fatalf("Invalid port: %v", err)
	}

	routes := s.routes()

	fmt.Println("\n" + `  _    _ _______ _______ _____
 | |  | |__   __|__   __|  __ \
 | |__| |  | |     | |  | |__) |
 |  __  |  | |     | |  |  ___/
 | |  | |  | |     | |  | |
 |_|  |_|  |_|     |_|  |_|

                                `)

	fmt.Printf("[INFO] Listening on :%d\n", port)

	httpServe := &http.Server{
		Addr:         fmt.Sprintf(":%d", port),
		Handler:      routes,
		ReadTimeout:  constants.SERVER_READ_TIMEOUT,
		WriteTimeout: constants.SERVER_WRITE_TIMEOUT,
		IdleTimeout:  constants.SERVER_IDLE_TIMEOUT,
	}

	return httpServe.ListenAndServe()
}
