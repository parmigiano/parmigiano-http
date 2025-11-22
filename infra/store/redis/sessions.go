package redis

import (
	"context"
	"errors"
	"fmt"
	"parmigiano/http/config"
	"parmigiano/http/infra/constants"
	"parmigiano/http/infra/encryption"
	"parmigiano/http/types"
	"time"

	"github.com/redis/go-redis/v9"
)

func sessionKey(key string) string {
	return fmt.Sprintf("session:%s", key)
}

func CreateSession(s *types.Session) (string, error) {
	sess, err := config.JSON.Marshal(s)
	if err != nil {
		return "", err
	}

	sessionId, err := encryption.Encrypt(string(sess))
	if err != nil {
		return "", err
	}

	key := sessionKey(sessionId)

	ctx, cancel := context.WithTimeout(context.Background(), 5*time.Second)
	defer cancel()

	exists, err := client.Exists(ctx, key).Result()
	if err != nil {
		return "", err
	}

	if exists == 1 {
		if err := client.Expire(ctx, key, constants.REDIS_SESSION_TTL).Err(); err != nil {
			return "", err
		}

		return sessionId, nil
	}

	if err := client.Set(ctx, key, sess, constants.REDIS_SESSION_TTL).Err(); err != nil {
		return "", err
	}

	return sessionId, nil
}

func GetSession(sessionId string) (*types.Session, error) {
	key := sessionKey(sessionId)

	ctx, cancel := context.WithTimeout(context.Background(), 5*time.Second)
	defer cancel()

	value, err := client.Get(ctx, key).Result()
	if errors.Is(err, redis.Nil) {
		return nil, nil
	}

	if err != nil {
		return nil, err
	}

	var session types.Session
	if err := config.JSON.Unmarshal([]byte(value), &session); err != nil {
		return nil, err
	}

	return &session, nil
}

func RefreshSession(sessionId string) error {
	key := sessionKey(sessionId)

	ctx, cancel := context.WithTimeout(context.Background(), 5*time.Second)
	defer cancel()

	_, err := client.Expire(ctx, key, constants.REDIS_SESSION_TTL).Result()
	return err
}

func DeleteSession(sessionId string) error {
	key := sessionKey(sessionId)

	ctx, cancel := context.WithTimeout(context.Background(), 5*time.Second)
	defer cancel()

	return client.Del(ctx, key).Err()
}
