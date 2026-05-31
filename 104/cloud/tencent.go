package cloud

import (
	"fmt"

	"github.com/tencentcloud/tencentcloud-sdk-go/tencentcloud/common"
	"github.com/tencentcloud/tencentcloud-sdk-go/tencentcloud/common/profile"
	vpc "github.com/tencentcloud/tencentcloud-sdk-go/tencentcloud/vpc/v20170312"
	ec2 "github.com/tencentcloud/tencentcloud-sdk-go/tencentcloud/cvm/v20170312"
)

type TencentClient struct {
	vpc *vpc.Client
	ec2 *ec2.Client
}

func NewTencentClient() (*TencentClient, error) {
	credential := common.NewCredential("", "")
	cpf := profile.NewClientProfile()
	cpf.HttpProfile.Endpoint = "vpc.tencentcloudapi.com"

	vpcClient, err := vpc.NewClient(credential, "ap-beijing", cpf)
	if err != nil {
		return nil, fmt.Errorf("创建腾讯云VPC客户端失败: %w", err)
	}

	cpf.HttpProfile.Endpoint = "cvm.tencentcloudapi.com"
	ec2Client, err := ec2.NewClient(credential, "ap-beijing", cpf)
	if err != nil {
		return nil, fmt.Errorf("创建腾讯云CVM客户端失败: %w", err)
	}

	return &TencentClient{
		vpc: vpcClient,
		ec2: ec2Client,
	}, nil
}

func (c *TencentClient) GetCloudType() CloudType {
	return Tencent
}

func (c *TencentClient) FetchResources() ([]Resource, error) {
	var resources []Resource

	vpcs, err := c.fetchVPCs()
	if err != nil {
		return nil, fmt.Errorf("获取VPC失败: %w", err)
	}
	resources = append(resources, vpcs...)

	subnets, err := c.fetchSubnets()
	if err != nil {
		return nil, fmt.Errorf("获取子网失败: %w", err)
	}
	resources = append(resources, subnets...)

	securityGroups, err := c.fetchSecurityGroups()
	if err != nil {
		return nil, fmt.Errorf("获取安全组失败: %w", err)
	}
	resources = append(resources, securityGroups...)

	instances, err := c.fetchInstances()
	if err != nil {
		return nil, fmt.Errorf("获取CVM实例失败: %w", err)
	}
	resources = append(resources, instances...)

	return resources, nil
}

func (c *TencentClient) fetchVPCs() ([]Resource, error) {
	var resources []Resource

	request := vpc.NewDescribeVpcsRequest()
	response, err := c.vpc.DescribeVpcs(request)
	if err != nil {
		return nil, err
	}

	for _, vpc := range response.Response.VpcSet {
		tags := make(map[string]string)
		for _, tag := range vpc.Tags {
			tags[*tag.Key] = *tag.Value
		}

		resources = append(resources, Resource{
			ID:         *vpc.VpcId,
			Name:       *vpc.VpcName,
			Type:       "vpc",
			Cloud:      Tencent,
			Attributes: map[string]interface{}{"cidr_block": *vpc.CidrBlock, "tags": tags},
		})
	}

	return resources, nil
}

func (c *TencentClient) fetchSubnets() ([]Resource, error) {
	var resources []Resource

	request := vpc.NewDescribeSubnetsRequest()
	response, err := c.vpc.DescribeSubnets(request)
	if err != nil {
		return nil, err
	}

	for _, subnet := range response.Response.SubnetSet {
		tags := make(map[string]string)
		for _, tag := range subnet.Tags {
			tags[*tag.Key] = *tag.Value
		}

		resources = append(resources, Resource{
			ID:         *subnet.SubnetId,
			Name:       *subnet.SubnetName,
			Type:       "subnet",
			Cloud:      Tencent,
			ParentID:   *subnet.VpcId,
			Attributes: map[string]interface{}{"cidr_block": *subnet.CidrBlock, "availability_zone": *subnet.Zone, "tags": tags},
		})
	}

	return resources, nil
}

func (c *TencentClient) fetchSecurityGroups() ([]Resource, error) {
	var resources []Resource

	request := vpc.NewDescribeSecurityGroupsRequest()
	response, err := c.vpc.DescribeSecurityGroups(request)
	if err != nil {
		return nil, err
	}

	for _, sg := range response.Response.SecurityGroupSet {
		tags := make(map[string]string)
		for _, tag := range sg.Tags {
			tags[*tag.Key] = *tag.Value
		}

		resources = append(resources, Resource{
			ID:         *sg.SecurityGroupId,
			Name:       *sg.SecurityGroupName,
			Type:       "security_group",
			Cloud:      Tencent,
			ParentID:   *sg.VpcId,
			Attributes: map[string]interface{}{"description": *sg.Description, "tags": tags},
		})
	}

	return resources, nil
}

func (c *TencentClient) fetchInstances() ([]Resource, error) {
	var resources []Resource

	request := ec2.NewDescribeInstancesRequest()
	response, err := c.ec2.DescribeInstances(request)
	if err != nil {
		return nil, err
	}

	for _, instance := range response.Response.InstanceSet {
		tags := make(map[string]string)
		for _, tag := range instance.Tags {
			tags[*tag.Key] = *tag.Value
		}

		resources = append(resources, Resource{
			ID:         *instance.InstanceId,
			Name:       *instance.InstanceName,
			Type:       "instance",
			Cloud:      Tencent,
			ParentID:   *instance.SubnetId,
			Attributes: map[string]interface{}{"instance_type": *instance.InstanceType, "image_id": *instance.ImageId, "tags": tags},
		})
	}

	return resources, nil
}