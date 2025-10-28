package util

import (
	"crypto/sha512"
	"encoding/base64"
	"strings"
)

func HashTo255(input string) string {
	hash := sha512.Sum512([]byte(input))

	b64 := base64.RawURLEncoding.EncodeToString(hash[:])

	repeated := strings.Repeat(b64, 4)
	return repeated[:255]
}
