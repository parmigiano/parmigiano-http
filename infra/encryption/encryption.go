package encryption

import (
	"crypto/aes"
	"crypto/cipher"
	"encoding/base64"
	"errors"
	"os"
)

var key []byte
var iv []byte

// Encrypt Шифрование данных
func Encrypt(plaintext string) (string, error) {
	key, _ = base64.StdEncoding.DecodeString(os.Getenv("SUPER_SECRET_KEY"))
	iv, _ = base64.StdEncoding.DecodeString(os.Getenv("IV"))

	block, err := aes.NewCipher(key)
	if err != nil {
		return "", errors.New("ошибка создания блока шифрования")
	}

	gsm, err := cipher.NewGCM(block)
	if err != nil {
		return "", errors.New("ошибка создания режима блочного шифра")
	}

	ciphertext := gsm.Seal(nil, iv, []byte(plaintext), nil)

	encodedCiphertext := base64.StdEncoding.EncodeToString(ciphertext)
	return encodedCiphertext, nil
}

// Decrypt Де-шифрование данных
func Decrypt(encodedCiphertext string) (string, error) {
	key, _ = base64.StdEncoding.DecodeString(os.Getenv("SUPER_SECRET_KEY"))
	iv, _ = base64.StdEncoding.DecodeString(os.Getenv("IV"))

	ciphertext, err := base64.StdEncoding.DecodeString(encodedCiphertext)
	if err != nil {
		return "", errors.New("ошибка декодирования зашифрованных данных")
	}

	block, err := aes.NewCipher(key)
	if err != nil {
		return "", errors.New("ошибка создания блока шифрования")
	}

	gsm, err := cipher.NewGCM(block)
	if err != nil {
		return "", errors.New("ошибка создания режима блочного шифра")
	}

	plaintext, err := gsm.Open(nil, iv, ciphertext, nil)
	if err != nil {
		return "", errors.New("ошибка расшифровки данных")
	}

	return string(plaintext), nil
}
