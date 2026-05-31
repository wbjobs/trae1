//go:build !linux

package transport

import "fmt"

func readDiskInfo(path string) (uint64, uint64, error) {
	return 0, 0, fmt.Errorf("disk info only available on Linux")
}
