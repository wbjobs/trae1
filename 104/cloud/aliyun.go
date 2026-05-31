package cloud

import (
	"fmt"

	"github.com/aliyun/alibaba-cloud-sdk-go/services/ecs"
)

type AliyunClient struct {
	ecs *ecs.Client
}

func NewAliyunClient() (*AliyunClient, error) {
	client, err := ecs.NewClientWithAccessKey("cn-hangzhou", "", "")
	if err != nil {
		return nil, fmt.Errorf("创建阿里云客户端失败: %w", err)
	}

	return &AliyunClient{
		ecs: client,
	}, nil
}

func (c *AliyunClient) GetCloudType() CloudType {
	return Aliyun
}

func (c *AliyunClient) FetchResources() ([]Resource, error) {
	var resources []Resource

	vpcs, err := c.fetchVPCs()
	if err != nil {
		return nil, fmt.Errorf("获取VPC失败: %w", err)
	}
	resources = append(resources, vpcs...)

	switches, err := c.fetchVSwitches()
	if err != nil {
		return nil, fmt.Errorf("获取交换机失败: %w", err)
	}
	resources = append(resources, switches...)

	securityGroups, err := c.fetchSecurityGroups()
	if err != nil {
		return nil, fmt.Errorf("获取安全组失败: %w", err)
	}
	resources = append(resources, securityGroups...)

	instances, err := c.fetchInstances()
	if err != nil {
		return nil, fmt.Errorf("获取ECS实例失败: %w", err)
	}
	resources = append(resources, instances...)

	return resources, nil
}

func (c *AliyunClient) fetchVPCs() ([]Resource, error) {
	var resources []Resource

	request := ecs.CreateDescribeVpcsRequest()
	request.Scheme = "https"

	response, err := c.ecs.DescribeVpcs(request)
	if err != nil {
		return nil, err
	}

	for _, vpc := range response.Vpcs.Vpc {
		tags := make(map[string]string)
		for _, tag := range vpc.Tags.Tag {
			tags[tag.TagKey] = tag.TagValue
		}

		resources = append(resources, Resource{
			ID:         vpc.VpcId,
			Name:       vpc.VpcName,
			Type:       "vpc",
			Cloud:      Aliyun,
			Attributes: map[string]interface{}{"cidr_block": vpc.CidrBlock, "tags": tags},
		})
	}

	return resources, nil
}

func (c *AliyunClient) fetchVSwitches() ([]Resource, error) {
	var resources []Resource

	request := ecs.CreateDescribeVSwitchesRequest()
	request.Scheme = "https"

	response, err := c.ecs.DescribeVSwitches(request)
	if err != nil {
		return nil, err
	}

	for _, vswitch := range response.VSwitches.VSwitch {
		tags := make(map[string]string)
		for _, tag := range vswitch.Tags.Tag {
			tags[tag.TagKey] = tag.TagValue
		}

		resources = append(resources, Resource{
			ID:         vswitch.VSwitchId,
			Name:       vswitch.VSwitchName,
			Type:       "subnet",
			Cloud:      Aliyun,
			ParentID:   vswitch.VpcId,
			Attributes: map[string]interface{}{"cidr_block": vswitch.CidrBlock, "zone_id": vswitch.ZoneId, "tags": tags},
		})
	}

	return resources, nil
}

func (c *AliyunClient) fetchSecurityGroups() ([]Resource, error) {
	var resources []Resource

	request := ecs.CreateDescribeSecurityGroupsRequest()
	request.Scheme = "https"

	response, err := c.ecs.DescribeSecurityGroups(request)
	if err != nil {
		return nil, err
	}

	for _, sg := range response.SecurityGroups.SecurityGroup {
		tags := make(map[string]string)
		for _, tag := range sg.Tags.Tag {
			tags[tag.TagKey] = tag.TagValue
		}

		resources = append(resources, Resource{
			ID:         sg.SecurityGroupId,
			Name:       sg.SecurityGroupName,
			Type:       "security_group",
			Cloud:      Aliyun,
			ParentID:   sg.VpcId,
			Attributes: map[string]interface{}{"description": sg.Description, "tags": tags},
		})
	}

	return resources, nil
}

func (c *AliyunClient) fetchInstances() ([]Resource, error) {
	var resources []Resource

	request := ecs.CreateDescribeInstancesRequest()
	request.Scheme = "https"

	response, err := c.ecs.DescribeInstances(request)
	if err != nil {
		return nil, err
	}

	for _, instance := range response.Instances.Instance {
		tags := make(map[string]string)
		for _, tag := range instance.Tags.Tag {
			tags[tag.TagKey] = tag.TagValue
		}

		resources = append(resources, Resource{
			ID:         instance.InstanceId,
			Name:       instance.InstanceName,
			Type:       "instance",
			Cloud:      Aliyun,
			ParentID:   instance.VSwitchId,
			Attributes: map[string]interface{}{"instance_type": instance.InstanceType, "image_id": instance.ImageId, "tags": tags},
		})
	}

	return resources, nil
}