package storage

import (
	"context"
	"fmt"
	"os"
	"time"

	"bastion/internal/config"

	"github.com/minio/minio-go/v7"
	"github.com/minio/minio-go/v7/pkg/credentials"
)

type MinIOClient struct {
	client   *minio.Client
	cfg      config.MinIOConfig
}

func NewMinIOClient(cfg config.MinIOConfig) (*MinIOClient, error) {
	client, err := minio.New(cfg.Endpoint, &minio.Options{
		Creds:  credentials.NewStaticV4(cfg.AccessKey, cfg.SecretKey, ""),
		Secure: cfg.UseSSL,
	})
	if err != nil {
		return nil, fmt.Errorf("create minio client: %w", err)
	}

	return &MinIOClient{
		client: client,
		cfg:    cfg,
	}, nil
}

func (m *MinIOClient) EnsureBucket(ctx context.Context) error {
	exists, err := m.client.BucketExists(ctx, m.cfg.BucketName)
	if err != nil {
		return fmt.Errorf("check bucket: %w", err)
	}
	if !exists {
		if err := m.client.MakeBucket(ctx, m.cfg.BucketName, minio.MakeBucketOptions{}); err != nil {
			return fmt.Errorf("create bucket: %w", err)
		}
		fmt.Printf("[MinIO] Created bucket '%s'\n", m.cfg.BucketName)
	}
	return nil
}

func (m *MinIOClient) UploadFile(ctx context.Context, objectKey, filePath string) error {
	info, err := m.client.FPutObject(ctx, m.cfg.BucketName, objectKey, filePath, minio.PutObjectOptions{
		ContentType: "application/x-asciinema",
	})
	if err != nil {
		return fmt.Errorf("upload file: %w", err)
	}
	fmt.Printf("[MinIO] Uploaded %s (%d bytes)\n", objectKey, info.Size)
	return nil
}

func (m *MinIOClient) GetPresignedURL(ctx context.Context, objectKey string, expire time.Duration) (string, error) {
	reqParams := make(map[string][]string)
	presignedURL, err := m.client.PresignedGetObject(ctx, m.cfg.BucketName, objectKey, expire, reqParams)
	if err != nil {
		return "", fmt.Errorf("generate presigned url: %w", err)
	}
	return presignedURL.String(), nil
}

func (m *MinIOClient) DeleteObject(ctx context.Context, objectKey string) error {
	return m.client.RemoveObject(ctx, m.cfg.BucketName, objectKey, minio.RemoveObjectOptions{})
}

func (m *MinIOClient) ListObjects(ctx context.Context, prefix string) ([]minio.ObjectInfo, error) {
	var objects []minio.ObjectInfo
	for object := range m.client.ListObjects(ctx, m.cfg.BucketName, minio.ListObjectsOptions{
		Prefix:    prefix,
		Recursive: true,
	}) {
		if object.Err != nil {
			return nil, object.Err
		}
		objects = append(objects, object)
	}
	return objects, nil
}

func UploadSessionRecording(ctx context.Context, m *MinIOClient, objectKey, localFilePath string) error {
	if _, err := os.Stat(localFilePath); os.IsNotExist(err) {
		return fmt.Errorf("recording file not found: %s", localFilePath)
	}

	if err := m.UploadFile(ctx, objectKey, localFilePath); err != nil {
		return fmt.Errorf("upload recording: %w", err)
	}

	fmt.Printf("[Storage] Session recording uploaded: %s -> %s\n", localFilePath, objectKey)
	return nil
}
