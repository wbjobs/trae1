package util

import (
	"crypto/hmac"
	"crypto/md5"
	"crypto/sha256"
	"encoding/hex"
	"encoding/json"
	"fmt"
	"sort"
	"strings"

	"golang.org/x/crypto/bcrypt"
)

func GenerateSignature(secret string, params map[string]string, method string, path string, body string, timestamp int64, nonce string) string {
	var paramPairs []string
	for k, v := range params {
		paramPairs = append(paramPairs, fmt.Sprintf("%s=%s", k, v))
	}
	sort.Strings(paramPairs)
	paramStr := strings.Join(paramPairs, "&")

	bodyHash := MD5Hash(body)

	rawStr := fmt.Sprintf("%s\n%s\n%s\n%d\n%s\n%s",
		method,
		path,
		paramStr,
		timestamp,
		nonce,
		bodyHash,
	)

	return HMACSHA256(secret, rawStr)
}

func HMACSHA256(secret string, data string) string {
	h := hmac.New(sha256.New, []byte(secret))
	h.Write([]byte(data))
	return hex.EncodeToString(h.Sum(nil))
}

func MD5Hash(data string) string {
	h := md5.New()
	h.Write([]byte(data))
	return hex.EncodeToString(h.Sum(nil))
}

func SHA256Hash(data string) string {
	h := sha256.New()
	h.Write([]byte(data))
	return hex.EncodeToString(h.Sum(nil))
}

func HashPassword(password string) (string, error) {
	bytes, err := bcrypt.GenerateFromPassword([]byte(password), bcrypt.DefaultCost)
	return string(bytes), err
}

func CheckPasswordHash(password, hash string) bool {
	err := bcrypt.CompareHashAndPassword([]byte(hash), []byte(password))
	return err == nil
}

func GenerateNonce() string {
	return fmt.Sprintf("%s%s",
		MD5Hash(fmt.Sprintf("%d", timeNowNano())),
		RandomString(8),
	)[:32]
}

func timeNowNano() int64 {
	return currentTimeNano()
}

func RandomString(length int) string {
	const charset = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789"
	result := make([]byte, length)
	for i := range result {
		result[i] = charset[int(randInt(int64(len(charset))))]
	}
	return string(result)
}

func BuildSignString(data map[string]interface{}) string {
	var keys []string
	for k := range data {
		keys = append(keys, k)
	}
	sort.Strings(keys)

	var pairs []string
	for _, k := range keys {
		v := data[k]
		switch val := v.(type) {
		case map[string]interface{}:
			pairs = append(pairs, fmt.Sprintf("%s=%s", k, BuildSignString(val)))
		case string:
			pairs = append(pairs, fmt.Sprintf("%s=%s", k, val))
		default:
			jsonBytes, _ := json.Marshal(v)
			pairs = append(pairs, fmt.Sprintf("%s=%s", k, string(jsonBytes)))
		}
	}
	return strings.Join(pairs, "&")
}

func VerifySignature(signature string, secret string, params map[string]string, method string, path string, body string, timestamp int64, nonce string) bool {
	expected := GenerateSignature(secret, params, method, path, body, timestamp, nonce)
	return hmac.Equal([]byte(signature), []byte(expected))
}
