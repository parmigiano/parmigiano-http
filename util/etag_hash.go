package util

import (
	"crypto/md5"
	"encoding/hex"
	"parmigiano/http/config"
)

func ETagHash(data interface{}) (string, error) {
	b, err := config.JSON.Marshal(data)
	if err != nil {
		return "", nil
	}

	hash := md5.Sum(b)
	return "\"" + hex.EncodeToString(hash[:]) + "\"", nil
}
