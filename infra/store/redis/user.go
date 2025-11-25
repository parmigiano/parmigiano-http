package redis

import (
	"context"
	"errors"
	"fmt"
	"parmigiano/http/config"
	"parmigiano/http/infra/store/postgres/models"
	"time"

	"github.com/redis/go-redis/v9"
)

func userMeKey(uid uint64) string {
	return fmt.Sprintf("user_me_%d", uid)
}

func SaveUserInfo(user *models.UserInfo) error {
	key := userMeKey(user.UserUid)

	ctx, cancel := context.WithTimeout(context.Background(), 5*time.Second)
	defer cancel()

	data, err := config.JSON.Marshal(user)
	if err != nil {
		return err
	}

	return client.Set(ctx, key, data, 0).Err()
}

func GetUserInfo(userUid uint64) (*models.UserInfo, error) {
	key := userMeKey(userUid)

	ctx, cancel := context.WithTimeout(context.Background(), 5*time.Second)
	defer cancel()

	value, err := client.Get(ctx, key).Result()
	if err != nil {
		if errors.Is(err, redis.Nil) {
			return nil, nil
		}

		return nil, err
	}

	var user models.UserInfo
	if err := config.JSON.Unmarshal([]byte(value), &user); err != nil {
		return nil, err
	}

	return &user, nil
}

func DeleteUserInfo(userUid uint64) error {
	key := userMeKey(userUid)

	ctx, cancel := context.WithTimeout(context.Background(), 5*time.Second)
	defer cancel()

	return client.Del(ctx, key).Err()
}
