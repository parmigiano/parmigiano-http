package redis

import (
	"context"
	"fmt"
	"os"
	"parmigiano/http/infra/constants"

	"github.com/redis/go-redis/v9"
)

var (
	client *redis.Client
)

func NewRedisDb() {
	client = redis.NewClient(&redis.Options{
		Addr:            fmt.Sprintf("%s:%s", os.Getenv("REDIS_HOST"), os.Getenv("REDIS_PORT")),
		Password:        os.Getenv("REDIS_PASSWORD"),
		DB:              int(constants.REDIS_DB),
		PoolSize:        int(constants.REDIS_POOL_SIZE),
		MinIdleConns:    int(constants.REDIS_MIN_IDLE_CONNS),
		PoolTimeout:     constants.REDIS_POOL_TIMEOUT,
		ConnMaxIdleTime: constants.REDIS_CONN_MAX_IDLE_TIME,
	})

	_, err := client.Ping(context.Background()).Result()
	if err != nil {
		fmt.Printf("Error connecting to Redis: %v\n", err)
		return
	}

	fmt.Printf("[INFO] Successfully connected to redis\n")
}
