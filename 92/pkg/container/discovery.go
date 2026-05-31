package container

import (
	"bytes"
	"context"
	"encoding/json"
	"fmt"
	"os"
	"os/exec"
	"path/filepath"
	"strings"
	"time"

	"github.com/io-qos/io-qos/internal/types"
)

type ContainerInfo struct {
	ID           string
	Name         string
	CgroupPath   string
	Runtime      string
	State        string
	CreatedAt    time.Time
	Labels       map[string]string
}

type Discoverer struct {
	cgroupRoot string
}

func NewDiscoverer() *Discoverer {
	return &Discoverer{
		cgroupRoot: "/sys/fs/cgroup",
	}
}

func NewDiscovererWithRoot(root string) *Discoverer {
	return &Discoverer{
		cgroupRoot: root,
	}
}

func (d *Discoverer) Discover(containerIDs []string) ([]ContainerInfo, error) {
	var containers []ContainerInfo
	var errs []string

	for _, id := range containerIDs {
		info, err := d.DiscoverSingle(id)
		if err != nil {
			errs = append(errs, fmt.Sprintf("%s: %v", id, err))
			continue
		}
		containers = append(containers, info)
	}

	if len(errs) > 0 && len(containers) == 0 {
		return nil, fmt.Errorf("failed to discover any containers: %s", strings.Join(errs, "; "))
	}

	return containers, nil
}

func (d *Discoverer) DiscoverSingle(containerID string) (ContainerInfo, error) {
	if strings.HasPrefix(containerID, "/") {
		return d.discoverFromCgroupPath(containerID)
	}

	info, err := d.discoverFromDocker(containerID)
	if err == nil {
		return info, nil
	}

	info, err = d.discoverFromCgroupfs(containerID)
	if err == nil {
		return info, nil
	}

	return ContainerInfo{}, fmt.Errorf("cannot discover container %s: not found via docker or cgroupfs", containerID)
}

func (d *Discoverer) discoverFromDocker(containerID string) (ContainerInfo, error) {
	ctx, cancel := context.WithTimeout(context.Background(), 5*time.Second)
	defer cancel()

	cmd := exec.CommandContext(ctx, "docker", "inspect", containerID)
	var stdout, stderr bytes.Buffer
	cmd.Stdout = &stdout
	cmd.Stderr = &stderr

	if err := cmd.Run(); err != nil {
		return ContainerInfo{}, fmt.Errorf("docker inspect failed: %v, stderr: %s", err, stderr.String())
	}

	var inspectResults []struct {
		ID              string            `json:"Id"`
		Name            string            `json:"Name"`
		State           struct {
			Status    string    `json:"Status"`
			StartedAt time.Time `json:"StartedAt"`
		} `json:"State"`
		Config struct {
			Labels map[string]string `json:"Labels"`
		} `json:"Config"`
		HostConfig struct {
			CgroupParent string `json:"CgroupParent"`
		} `json:"HostConfig"`
	}

	if err := json.Unmarshal(stdout.Bytes(), &inspectResults); err != nil {
		return ContainerInfo{}, fmt.Errorf("failed to parse docker inspect output: %w", err)
	}

	if len(inspectResults) == 0 {
		return ContainerInfo{}, fmt.Errorf("no container found")
	}

	result := inspectResults[0]
	cgroupPath := d.findDockerCgroupPath(result.ID)

	if cgroupPath == "" {
		cgroupPath = filepath.Join("docker", result.ID)
	}

	return ContainerInfo{
		ID:         result.ID,
		Name:       strings.TrimPrefix(result.Name, "/"),
		CgroupPath: cgroupPath,
		Runtime:    "docker",
		State:      result.State.Status,
		CreatedAt:  result.State.StartedAt,
		Labels:     result.Config.Labels,
	}, nil
}

func (d *Discoverer) findDockerCgroupPath(containerID string) string {
	paths := []string{
		filepath.Join("system.slice", fmt.Sprintf("docker-%s.scope", containerID)),
		filepath.Join("docker", containerID),
		filepath.Join("kubepods", "besteffort", "pod"+containerID[:12]),
	}

	for _, p := range paths {
		fullPath := filepath.Join(d.cgroupRoot, p)
		if _, err := os.Stat(fullPath); err == nil {
			return p
		}
	}

	return d.searchCgroupForContainer(containerID)
}

func (d *Discoverer) searchCgroupForContainer(containerID string) string {
	var foundPath string
	filepath.Walk(d.cgroupRoot, func(path string, info os.FileInfo, err error) error {
		if err != nil || !info.IsDir() {
			return nil
		}

		relPath, _ := filepath.Rel(d.cgroupRoot, path)
		if strings.Contains(relPath, containerID[:12]) {
			foundPath = relPath
			return filepath.SkipDir
		}
		return nil
	})

	return foundPath
}

func (d *Discoverer) discoverFromCgroupfs(containerID string) (ContainerInfo, error) {
	cgroupPath := d.searchCgroupForContainer(containerID)
	if cgroupPath == "" {
		return ContainerInfo{}, fmt.Errorf("container not found in cgroupfs")
	}

	return ContainerInfo{
		ID:         containerID,
		Name:       containerID,
		CgroupPath: cgroupPath,
		Runtime:    "unknown",
		State:      "unknown",
	}, nil
}

func (d *Discoverer) discoverFromCgroupPath(cgroupPath string) (ContainerInfo, error) {
	fullPath := filepath.Join(d.cgroupRoot, cgroupPath)
	if _, err := os.Stat(fullPath); err != nil {
		return ContainerInfo{}, fmt.Errorf("cgroup path does not exist: %w", err)
	}

	id := filepath.Base(cgroupPath)
	name := id

	if strings.HasPrefix(id, "docker-") && strings.HasSuffix(id, ".scope") {
		id = strings.TrimSuffix(strings.TrimPrefix(id, "docker-"), ".scope")
		name = id
	}

	return ContainerInfo{
		ID:         id,
		Name:       name,
		CgroupPath: cgroupPath,
		Runtime:    "unknown",
		State:      "unknown",
	}, nil
}

func (d *Discoverer) DiscoverAll() ([]ContainerInfo, error) {
	var containers []ContainerInfo

	dockerContainers, err := d.discoverAllDocker()
	if err == nil {
		containers = append(containers, dockerContainers...)
	}

	cgroupContainers, err := d.discoverAllFromCgroup()
	if err == nil {
		for _, cc := range cgroupContainers {
			found := false
			for _, dc := range containers {
				if dc.ID == cc.ID {
					found = true
					break
				}
			}
			if !found {
				containers = append(containers, cc)
			}
		}
	}

	return containers, nil
}

func (d *Discoverer) discoverAllDocker() ([]ContainerInfo, error) {
	ctx, cancel := context.WithTimeout(context.Background(), 10*time.Second)
	defer cancel()

	cmd := exec.CommandContext(ctx, "docker", "ps", "-q", "--no-trunc")
	var stdout, stderr bytes.Buffer
	cmd.Stdout = &stdout
	cmd.Stderr = &stderr

	if err := cmd.Run(); err != nil {
		return nil, fmt.Errorf("docker ps failed: %v", err)
	}

	ids := strings.Fields(stdout.String())
	return d.Discover(ids)
}

func (d *Discoverer) discoverAllFromCgroup() ([]ContainerInfo, error) {
	var containers []ContainerInfo

	searchPaths := []string{
		"docker",
		"system.slice",
		"kubepods",
	}

	for _, sp := range searchPaths {
		fullPath := filepath.Join(d.cgroupRoot, sp)
		if _, err := os.Stat(fullPath); err != nil {
			continue
		}

		filepath.Walk(fullPath, func(path string, info os.FileInfo, err error) error {
			if err != nil {
				return nil
			}

			if info.IsDir() {
				relPath, _ := filepath.Rel(d.cgroupRoot, path)
				if d.isContainerCgroup(path) {
					base := filepath.Base(path)
					id := base
					name := base

					if strings.HasPrefix(base, "docker-") && strings.HasSuffix(base, ".scope") {
						id = strings.TrimSuffix(strings.TrimPrefix(base, "docker-"), ".scope")
						name = id[:12]
					}

					containers = append(containers, ContainerInfo{
						ID:         id,
						Name:       name,
						CgroupPath: relPath,
						Runtime:    "cgroup",
						State:      "running",
					})
				}
			}
			return nil
		})
	}

	return containers, nil
}

func (d *Discoverer) isContainerCgroup(path string) bool {
	ioMaxPath := filepath.Join(path, "io.max")
	ioStatPath := filepath.Join(path, "io.stat")

	_, err1 := os.Stat(ioMaxPath)
	_, err2 := os.Stat(ioStatPath)

	return err1 == nil || err2 == nil
}

func (d *Discoverer) GetCgroupPath(containerID string) (string, error) {
	info, err := d.DiscoverSingle(containerID)
	if err != nil {
		return "", err
	}
	return info.CgroupPath, nil
}

func ResolveConfig(config types.ContainerConfig, cgroupRoot string) (types.ContainerConfig, error) {
	if config.CgroupPath != "" {
		fullPath := filepath.Join(cgroupRoot, config.CgroupPath)
		if _, err := os.Stat(fullPath); err == nil {
			return config, nil
		}
	}

	d := NewDiscovererWithRoot(cgroupRoot)
	info, err := d.DiscoverSingle(config.ContainerID)
	if err != nil {
		return config, err
	}

	config.CgroupPath = info.CgroupPath
	config.ContainerName = info.Name
	return config, nil
}
