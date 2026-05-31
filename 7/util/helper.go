package util

import (
	"math/rand"
	"time"
)

func currentTimeNano() int64 {
	return time.Now().UnixNano()
}

func randInt(max int64) int64 {
	return rand.New(rand.NewSource(time.Now().UnixNano())).Int63n(max)
}

func GetCurrentTimestamp() int64 {
	return time.Now().Unix()
}

func GetTimestampDiff(timestamp int64) int64 {
	return GetCurrentTimestamp() - timestamp
}

func FormatTime(t time.Time) string {
	return t.Format("2006-01-02 15:04:05")
}
