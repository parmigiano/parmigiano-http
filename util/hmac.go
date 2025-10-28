package util

import (
	"crypto/hmac"
	"crypto/sha256"
	"encoding/base64"
	"fmt"
	"os"
	"time"
)

func SignUser(uuid string, exp int64) string {
	payload := fmt.Sprintf("%s:%d", uuid, exp)
	hash := hmac.New(sha256.New, []byte(os.Getenv("SUPER_SECRET_KEY")))
	hash.Write([]byte(payload))

	return base64.RawURLEncoding.EncodeToString(hash.Sum(nil))
}

func VerifySignUser(uuid string, exp int64, sig string) bool {
	if time.Now().Unix() > exp {
		return false
	}

	expected := SignUser(uuid, exp)
	return hmac.Equal([]byte(expected), []byte(sig))
}
