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

func chatSettingKey(chatId uint64) string {
	return fmt.Sprintf("chat:setting:%d", chatId)
}

func SetCacheChatSetting(setting *models.ChatSetting) error {
	key := chatSettingKey(setting.ChatID)

	ctx, cancel := context.WithTimeout(context.Background(), 5*time.Second)
	defer cancel()

	b, err := config.JSON.Marshal(setting)
	if err != nil {
		return err
	}

	return client.Set(ctx, key, b, 24*time.Hour).Err()
}

func GetCachedChatSetting(chatId uint64) (*models.ChatSetting, error) {
	key := chatSettingKey(chatId)

	ctx, cancel := context.WithTimeout(context.Background(), 5*time.Second)
	defer cancel()

	data, err := client.Get(ctx, key).Bytes()
	if errors.Is(err, redis.Nil) {
		return nil, nil
	}

	if err != nil {
		return nil, err
	}

	var cs models.ChatSetting
	if err := config.JSON.Unmarshal(data, &cs); err != nil {
		return nil, err
	}

	return &cs, nil
}

func DeleteChatSettingCache(chatId uint64) error {
	key := chatSettingKey(chatId)

	ctx, cancel := context.WithTimeout(context.Background(), 5*time.Second)
	defer cancel()

	return client.Del(ctx, key).Err()
}
