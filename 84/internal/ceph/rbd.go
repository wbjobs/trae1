package ceph

import (
	"fmt"
	"time"

	"github.com/ceph/go-ceph/rados"
	"github.com/ceph/go-ceph/rbd"
	"go.uber.org/zap"
)

type RBDClient struct {
	conn    *rados.Conn
	ioctx   *rados.IOContext
	logger  *zap.Logger
	pool    string
}

type SnapshotInfo struct {
	Name        string
	ID          uint64
	Size        uint64
	CreatedAt   time.Time
	IsProtected bool
}

type Config struct {
	ClusterName string
	UserName    string
	Keyring     string
	Monitors    []string
	Pool        string
}

func NewRBDClient(cfg *Config, logger *zap.Logger) (*RBDClient, error) {
	conn, err := rados.NewConnWithUser(cfg.UserName)
	if err != nil {
		return nil, fmt.Errorf("failed to create RADOS connection: %w", err)
	}

	if cfg.ClusterName != "" {
		conn.SetClusterName(cfg.ClusterName)
	}

	if len(cfg.Monitors) > 0 {
		if err := conn.SetConfigOption("mon_host", joinMonitors(cfg.Monitors)); err != nil {
			return nil, fmt.Errorf("failed to set mon_host: %w", err)
		}
	}

	if cfg.Keyring != "" {
		if err := conn.SetConfigOption("keyring", cfg.Keyring); err != nil {
			logger.Warn("Failed to set keyring path", zap.Error(err))
		}
	}

	if err := conn.Connect(); err != nil {
		return nil, fmt.Errorf("failed to connect to RADOS: %w", err)
	}

	ioctx, err := conn.OpenIOContext(cfg.Pool)
	if err != nil {
		conn.Shutdown()
		return nil, fmt.Errorf("failed to open IO context for pool %s: %w", cfg.Pool, err)
	}

	return &RBDClient{
		conn:  conn,
		ioctx: ioctx,
		logger: logger,
		pool:  cfg.Pool,
	}, nil
}

func joinMonitors(mons []string) string {
	result := ""
	for i, mon := range mons {
		if i > 0 {
			result += ","
		}
		result += mon
	}
	return result
}

func (c *RBDClient) Close() {
	if c.ioctx != nil {
		c.ioctx.Destroy()
	}
	if c.conn != nil {
		c.conn.Shutdown()
	}
}

func (c *RBDClient) CreateSnapshot(imageName, snapshotName string) error {
	img, err := rbd.OpenImage(c.ioctx, imageName, rbd.NoSnapshot)
	if err != nil {
		return fmt.Errorf("failed to open image %s: %w", imageName, err)
	}
	defer img.Close()

	err = img.CreateSnapshot(snapshotName)
	if err != nil {
		return fmt.Errorf("failed to create snapshot %s for image %s: %w", snapshotName, imageName, err)
	}

	c.logger.Info("Snapshot created successfully",
		zap.String("image", imageName),
		zap.String("snapshot", snapshotName),
	)
	return nil
}

func (c *RBDClient) ListSnapshots(imageName string) ([]SnapshotInfo, error) {
	img, err := rbd.OpenImage(c.ioctx, imageName, rbd.NoSnapshot)
	if err != nil {
		return nil, fmt.Errorf("failed to open image %s: %w", imageName, err)
	}
	defer img.Close()

	snapShots, err := img.GetSnapShots()
	if err != nil {
		return nil, fmt.Errorf("failed to list snapshots for image %s: %w", imageName, err)
	}

	var result []SnapshotInfo
	for _, snap := range snapShots {
		result = append(result, SnapshotInfo{
			Name:      snap.Name,
			ID:        snap.Id,
			Size:      snap.Size,
			CreatedAt: time.Unix(int64(snap.Timestamp), 0),
		})
	}
	return result, nil
}

func (c *RBDClient) DeleteSnapshot(imageName, snapshotName string) error {
	img, err := rbd.OpenImage(c.ioctx, imageName, rbd.NoSnapshot)
	if err != nil {
		return fmt.Errorf("failed to open image %s: %w", imageName, err)
	}
	defer img.Close()

	err = img.RemoveSnapshot(snapshotName)
	if err != nil {
		return fmt.Errorf("failed to remove snapshot %s from image %s: %w", snapshotName, imageName, err)
	}

	c.logger.Info("Snapshot deleted successfully",
		zap.String("image", imageName),
		zap.String("snapshot", snapshotName),
	)
	return nil
}

func (c *RBDClient) ProtectSnapshot(imageName, snapshotName string) error {
	img, err := rbd.OpenImage(c.ioctx, imageName, rbd.NoSnapshot)
	if err != nil {
		return fmt.Errorf("failed to open image %s: %w", imageName, err)
	}
	defer img.Close()

	snap := img.GetSnapshot(snapshotName)
	err = snap.Protect()
	if err != nil {
		return fmt.Errorf("failed to protect snapshot %s: %w", snapshotName, err)
	}

	c.logger.Info("Snapshot protected",
		zap.String("image", imageName),
		zap.String("snapshot", snapshotName),
	)
	return nil
}

func (c *RBDClient) UnprotectSnapshot(imageName, snapshotName string) error {
	img, err := rbd.OpenImage(c.ioctx, imageName, rbd.NoSnapshot)
	if err != nil {
		return fmt.Errorf("failed to open image %s: %w", imageName, err)
	}
	defer img.Close()

	snap := img.GetSnapshot(snapshotName)
	err = snap.Unprotect()
	if err != nil {
		return fmt.Errorf("failed to unprotect snapshot %s: %w", snapshotName, err)
	}

	c.logger.Info("Snapshot unprotected",
		zap.String("image", imageName),
		zap.String("snapshot", snapshotName),
	)
	return nil
}

func (c *RBDClient) CloneImage(parentImage, parentSnapshot, newImageName string, size uint64) error {
	img, err := rbd.OpenImage(c.ioctx, parentImage, rbd.NoSnapshot)
	if err != nil {
		return fmt.Errorf("failed to open parent image %s: %w", parentImage, err)
	}
	defer img.Close()

	snap := img.GetSnapshot(parentSnapshot)

	options := rbd.NewRbdImageOptions()
	defer options.Destroy()

	err = options.SetUint64(rbd.RbdImageOptionFeatures, uint64(rbd.RbdFeatureLayering))
	if err != nil {
		return fmt.Errorf("failed to set image features: %w", err)
	}

	if size > 0 {
		err = options.SetUint64(rbd.RbdImageOptionSize, size)
		if err != nil {
			return fmt.Errorf("failed to set image size: %w", err)
		}
	}

	err = snap.Clone(c.ioctx, newImageName, options)
	if err != nil {
		return fmt.Errorf("failed to clone image %s from snapshot %s: %w", newImageName, parentSnapshot, err)
	}

	c.logger.Info("Image cloned successfully",
		zap.String("parent_image", parentImage),
		zap.String("snapshot", parentSnapshot),
		zap.String("new_image", newImageName),
	)
	return nil
}

func (c *RBDClient) CreateImage(name string, size uint64) error {
	options := rbd.NewRbdImageOptions()
	defer options.Destroy()

	err := options.SetUint64(rbd.RbdImageOptionSize, size)
	if err != nil {
		return fmt.Errorf("failed to set image size: %w", err)
	}

	err = options.SetUint64(rbd.RbdImageOptionFeatures, uint64(rbd.RbdFeatureLayering))
	if err != nil {
		return fmt.Errorf("failed to set image features: %w", err)
	}

	err = rbd.CreateImage(c.ioctx, name, size, options)
	if err != nil {
		return fmt.Errorf("failed to create image %s: %w", name, err)
	}

	c.logger.Info("Image created successfully",
		zap.String("image", name),
		zap.Uint64("size", size),
	)
	return nil
}

func (c *RBDClient) DeleteImage(name string) error {
	err := rbd.RemoveImage(c.ioctx, name)
	if err != nil {
		return fmt.Errorf("failed to delete image %s: %w", name, err)
	}

	c.logger.Info("Image deleted successfully",
		zap.String("image", name),
	)
	return nil
}

func (c *RBDClient) GetImageInfo(name string) (*rbd.ImageInfo, error) {
	img, err := rbd.OpenImage(c.ioctx, name, rbd.NoSnapshot)
	if err != nil {
		return nil, fmt.Errorf("failed to open image %s: %w", name, err)
	}
	defer img.Close()

	return img.Stat()
}

func (c *RBDClient) RollbackToSnapshot(imageName, snapshotName string) error {
	img, err := rbd.OpenImage(c.ioctx, imageName, rbd.NoSnapshot)
	if err != nil {
		return fmt.Errorf("failed to open image %s: %w", imageName, err)
	}
	defer img.Close()

	snap := img.GetSnapshot(snapshotName)
	err = snap.Rollback()
	if err != nil {
		return fmt.Errorf("failed to rollback to snapshot %s: %w", snapshotName, err)
	}

	c.logger.Info("Rollback to snapshot completed",
		zap.String("image", imageName),
		zap.String("snapshot", snapshotName),
	)
	return nil
}

func (c *RBDClient) ListImages() ([]string, error) {
	return rbd.GetImageNames(c.ioctx)
}

type ConsistencyGroup struct {
	Name      string
	Images    []string
	Snapshots map[string]string
}

func (c *RBDClient) CreateConsistencySnapshot(group *ConsistencyGroup, snapshotPrefix string) error {
	timestamp := time.Now().Format("20060102150405")
	group.Snapshots = make(map[string]string)

	for _, image := range group.Images {
		snapshotName := fmt.Sprintf("%s-%s-%s", snapshotPrefix, image, timestamp)
		err := c.CreateSnapshot(image, snapshotName)
		if err != nil {
			for img, snap := range group.Snapshots {
				if rollbackErr := c.DeleteSnapshot(img, snap); rollbackErr != nil {
					c.logger.Error("Failed to rollback snapshot during consistency group failure",
						zap.String("image", img),
						zap.String("snapshot", snap),
						zap.Error(rollbackErr),
					)
				}
			}
			return fmt.Errorf("consistency group snapshot failed for image %s: %w", image, err)
		}

		err = c.ProtectSnapshot(image, snapshotName)
		if err != nil {
			c.logger.Warn("Failed to protect consistency snapshot",
				zap.String("image", image),
				zap.String("snapshot", snapshotName),
				zap.Error(err),
			)
		}

		group.Snapshots[image] = snapshotName
	}

	c.logger.Info("Consistency group snapshot created",
		zap.String("group", group.Name),
		zap.Int("image_count", len(group.Images)),
	)
	return nil
}

func (c *RBDClient) DeleteConsistencySnapshots(snapshots map[string]string) error {
	var errs []error
	for image, snapshot := range snapshots {
		err := c.UnprotectSnapshot(image, snapshot)
		if err != nil {
			c.logger.Warn("Failed to unprotect snapshot",
				zap.String("image", image),
				zap.String("snapshot", snapshot),
				zap.Error(err),
			)
		}

		err = c.DeleteSnapshot(image, snapshot)
		if err != nil {
			errs = append(errs, fmt.Errorf("failed to delete snapshot %s from image %s: %w", snapshot, image, err))
		}
	}
	if len(errs) > 0 {
		return fmt.Errorf("errors deleting consistency snapshots: %v", errs)
	}
	return nil
}

func (c *RBDClient) FlattenImage(imageName string) error {
	img, err := rbd.OpenImage(c.ioctx, imageName, rbd.NoSnapshot)
	if err != nil {
		return fmt.Errorf("failed to open image %s: %w", imageName, err)
	}
	defer img.Close()

	err = img.Flatten()
	if err != nil {
		return fmt.Errorf("failed to flatten image %s: %w", imageName, err)
	}

	c.logger.Info("Image flattened successfully",
		zap.String("image", imageName),
	)
	return nil
}
