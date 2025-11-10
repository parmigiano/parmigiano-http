package main

import (
	"fmt"
	"log"
	"net/http"
	"os"
	"parmigiano/http/config"
	"parmigiano/http/infra/constants"
	"runtime"
	"strconv"
	"time"
)

func (s *httpServer) httpStart() error {
	port, err := strconv.Atoi(config.HttpServerPort)
	if err != nil {
		log.Fatalf("Invalid port: %v", err)
	}

	routes := s.routes()

	fmt.Printf("\n[%v] [INFO] Http server started %s:%d\n", time.Now().Format("2006-01-02 15:04:05"), config.ServerAddr, port)
	fmt.Printf("[%v] [INFO] Proccess PID: %d, Version: beta\n", time.Now().Format("2006-01-02 15:04:05"), os.Getpid())
	fmt.Printf("[%v] [INFO] Golang version: %s\n\n", time.Now().Format("2006-01-02 15:04:05"), runtime.Version())

	httpServe := &http.Server{
		Addr:         fmt.Sprintf(":%d", port),
		Handler:      routes,
		ReadTimeout:  constants.SERVER_READ_TIMEOUT,
		WriteTimeout: constants.SERVER_WRITE_TIMEOUT,
		IdleTimeout:  constants.SERVER_IDLE_TIMEOUT,
	}

	return httpServe.ListenAndServe()
}
