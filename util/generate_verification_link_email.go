package util

import (
	"crypto/hmac"
	"crypto/sha256"
	"encoding/base64"
	"fmt"
	"net/url"
	"os"
	"parmigiano/http/infra/constants"
	"strconv"
	"time"
)

func GenerateVerificationEmailLink(userUid uint64) string {
	expires := time.Now().Add(constants.EMAIL_LINK_TIMEOUT).Unix()

	userUidStr := strconv.Itoa(int(userUid))

	payload := fmt.Sprintf("%s:%d", userUidStr, expires)

	mac := hmac.New(sha256.New, []byte(os.Getenv("SUPER_SECRET_KEY")))
	mac.Write([]byte(payload))
	signature := base64.RawURLEncoding.EncodeToString(mac.Sum(nil))

	values := url.Values{}
	values.Set("uid", userUidStr)
	values.Set("exp", fmt.Sprintf("%d", expires))
	values.Set("sig", signature)

	return fmt.Sprintf("%s/auth/confirm?%s", os.Getenv("SERVER_BASE_ADDR"), values.Encode())
}

func ValidateEmailConfirmLink(userUid, expStr, sig string) bool {
	exp, err := strconv.ParseInt(expStr, 10, 64)
	if err != nil {
		return false
	}

	if time.Now().Unix() > exp {
		return false
	}

	payload := fmt.Sprintf("%s:%d", userUid, exp)

	mac := hmac.New(sha256.New, []byte(os.Getenv("SUPER_SECRET_KEY")))
	mac.Write([]byte(payload))

	expectedSig := base64.RawURLEncoding.EncodeToString(mac.Sum(nil))

	return hmac.Equal([]byte(sig), []byte(expectedSig))
}
