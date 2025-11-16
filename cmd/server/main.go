package main

import (
	"database/sql"
	"os"
	"parmigiano/http/infra/constants"
	"parmigiano/http/infra/logger"
	"parmigiano/http/infra/store/postgres"
	"parmigiano/http/infra/store/postgres/store"
	"parmigiano/http/pkg/s3"

	"github.com/joho/godotenv"
)

type httpServer struct {
	db     *sql.DB
	logger *logger.Logger
	store  store.Storage
	// usecase usecase.UseCase
}

func main() {
	if err := godotenv.Load(); err != nil {
		panic(err)
	}

	// logger
	log := logger.NewLogger()

	// connection db
	db, err := postgres.New(os.Getenv("DB_ADDR"), int(constants.DB_MAX_OPEN_CONNS), int(constants.DB_MAX_IDLE_CONNS), constants.DB_MAX_IDLE_TIME)
	if err != nil {
		log.Error(err.Error())
		return
	}
	defer db.Close()

	store := store.NewStorage(db, log)

	// initial redis
	// redis.NewRedisDb()

	// initial s3 storage
	s3.InitS3()

	server := &httpServer{
		db:     db,
		logger: log,
		store:  store,
	}

	// cron
	server.checkUserIfEmailNotConfirmed()

	// start http server
	if err := server.httpStart(); err != nil {
		log.Error(err.Error())
	}
}
