package cloud

import (
	"fmt"
)

type CloudType string

const (
	AWS      CloudType = "aws"
	Aliyun   CloudType = "aliyun"
	Tencent  CloudType = "tencent"
	AllClouds CloudType = "all"
)

type CloudClient interface {
	FetchResources() ([]Resource, error)
	GetCloudType() CloudType
}

type Resource struct {
	ID           string                 `json:"id"`
	Name         string                 `json:"name"`
	Type         string                 `json:"type"`
	Cloud        CloudType              `json:"cloud"`
	Attributes   map[string]interface{} `json:"attributes"`
	DependsOn    []string               `json:"depends_on"`
	ParentID     string                 `json:"parent_id,omitempty"`
}

type CloudClients struct {
	AWS      *AWSClient
	Aliyun   *AliyunClient
	Tencent  *TencentClient
}

func NewCloudClients(cloudType string) (*CloudClients, error) {
	clients := &CloudClients{}

	switch CloudType(cloudType) {
	case AWS, AllClouds:
		awsClient, err := NewAWSClient()
		if err != nil {
			return nil, fmt.Errorf("创建AWS客户端失败: %w", err)
		}
		clients.AWS = awsClient
	case Aliyun, AllClouds:
		aliyunClient, err := NewAliyunClient()
		if err != nil {
			return nil, fmt.Errorf("创建阿里云客户端失败: %w", err)
		}
		clients.Aliyun = aliyunClient
	case Tencent, AllClouds:
		tencentClient, err := NewTencentClient()
		if err != nil {
			return nil, fmt.Errorf("创建腾讯云客户端失败: %w", err)
		}
		clients.Tencent = tencentClient
	}

	return clients, nil
}

func FetchAllResources(clients *CloudClients) ([]Resource, error) {
	var allResources []Resource

	if clients.AWS != nil {
		resources, err := clients.AWS.FetchResources()
		if err != nil {
			return nil, fmt.Errorf("获取AWS资源失败: %w", err)
		}
		allResources = append(allResources, resources...)
	}

	if clients.Aliyun != nil {
		resources, err := clients.Aliyun.FetchResources()
		if err != nil {
			return nil, fmt.Errorf("获取阿里云资源失败: %w", err)
		}
		allResources = append(allResources, resources...)
	}

	if clients.Tencent != nil {
		resources, err := clients.Tencent.FetchResources()
		if err != nil {
			return nil, fmt.Errorf("获取腾讯云资源失败: %w", err)
		}
		allResources = append(allResources, resources...)
	}

	return allResources, nil
}