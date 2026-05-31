//go:build !linux

package resource

import "fmt"

func readDiskInfo(path string) (total, free uint64, err error) {
	return 0, 0, fmt.Errorf("disk info only available on Linux")
}
