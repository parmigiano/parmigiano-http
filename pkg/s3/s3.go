package s3

import (
	"context"
	"fmt"
	"os"
	"parmigiano/http/infra/logger"

	"github.com/minio/minio-go/v7"
	"github.com/minio/minio-go/v7/pkg/credentials"
)

var log *logger.Logger = logger.NewLogger()
var Client *minio.Client
var BucketName string

func InitS3() {
	endpoint := os.Getenv("S3_ENDPOINT")
	accessKey := os.Getenv("S3_ACCESS_KEY")
	secretKey := os.Getenv("S3_SECRET_KEY")
	BucketName = os.Getenv("S3_BUCKET")

	client, err := minio.New(endpoint, &minio.Options{
		Creds:  credentials.NewStaticV4(accessKey, secretKey, ""),
		Secure: true,
	})
	if err != nil {
		log.Error("%v", err)
	}

	Client = client

	exists, err := Client.BucketExists(context.Background(), BucketName)
	if err != nil {
		log.Error("%v", err)
	}

	if !exists {
		err := Client.MakeBucket(context.Background(), BucketName, minio.MakeBucketOptions{})
		if err != nil {
			log.Error("%v", err)
		}

		fmt.Printf("[INFO] Bucket %s is created\n", BucketName)
	}

	fmt.Printf("[INFO] S3 connected (%s)\n", endpoint)
}
