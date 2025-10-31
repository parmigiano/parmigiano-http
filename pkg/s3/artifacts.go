package s3

import (
	"context"
	"fmt"
	"os"
	"parmigiano/http/util"
	"path/filepath"

	"github.com/google/uuid"
	"github.com/minio/minio-go/v7"
)

func UploadImageFile(userUid uint64, filePath string, contentType string) (string, error) {
	objectName := fmt.Sprintf("%d/%s%s", userUid, uuid.New().String(), filepath.Ext(filePath))

	_, err := Client.FPutObject(context.Background(), BucketName, objectName, filePath, minio.PutObjectOptions{
		ContentType: contentType,
	})
	if err != nil {
		return "", err
	}

	return fmt.Sprintf("%s/%s", os.Getenv("S3_VIRTUAL_HOSTED_STYLE"), objectName), nil
}

func DeleteFile(fileURL string) error {
	key := util.ExtractKeyFromURL(fileURL)
	if key == "" {
		return fmt.Errorf("не удалось извлечь ключ из URL: %s", fileURL)
	}

	err := Client.RemoveObject(
		context.Background(),
		BucketName,
		key,
		minio.RemoveObjectOptions{},
	)
	if err != nil {
		return fmt.Errorf("ошибка удаления файла из S3: %w", err)
	}

	return nil
}
