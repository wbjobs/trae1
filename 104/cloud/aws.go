package cloud

import (
	"context"
	"fmt"

	"github.com/aws/aws-sdk-go-v2/aws"
	"github.com/aws/aws-sdk-go-v2/config"
	"github.com/aws/aws-sdk-go-v2/service/ec2"
	"github.com/aws/aws-sdk-go-v2/service/ec2/types"
)

type AWSClient struct {
	ec2 *ec2.Client
}

func NewAWSClient() (*AWSClient, error) {
	cfg, err := config.LoadDefaultConfig(context.TODO())
	if err != nil {
		return nil, fmt.Errorf("加载AWS配置失败: %w", err)
	}

	return &AWSClient{
		ec2: ec2.NewFromConfig(cfg),
	}, nil
}

func (c *AWSClient) GetCloudType() CloudType {
	return AWS
}

func (c *AWSClient) FetchResources() ([]Resource, error) {
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
		return nil, fmt.Errorf("获取EC2实例失败: %w", err)
	}
	resources = append(resources, instances...)

	return resources, nil
}

func (c *AWSClient) fetchVPCs() ([]Resource, error) {
	var resources []Resource

	result, err := c.ec2.DescribeVpcs(context.TODO(), &ec2.DescribeVpcsInput{})
	if err != nil {
		return nil, err
	}

	for _, vpc := range result.Vpcs {
		tags := make(map[string]string)
		for _, tag := range vpc.Tags {
			tags[*tag.Key] = *tag.Value
		}

		resources = append(resources, Resource{
			ID:         aws.ToString(vpc.VpcId),
			Name:       tags["Name"],
			Type:       "vpc",
			Cloud:      AWS,
			Attributes: map[string]interface{}{"cidr_block": aws.ToString(vpc.CidrBlock), "tags": tags},
		})
	}

	return resources, nil
}

func (c *AWSClient) fetchSubnets() ([]Resource, error) {
	var resources []Resource

	result, err := c.ec2.DescribeSubnets(context.TODO(), &ec2.DescribeSubnetsInput{})
	if err != nil {
		return nil, err
	}

	for _, subnet := range result.Subnets {
		tags := make(map[string]string)
		for _, tag := range subnet.Tags {
			tags[*tag.Key] = *tag.Value
		}

		resources = append(resources, Resource{
			ID:         aws.ToString(subnet.SubnetId),
			Name:       tags["Name"],
			Type:       "subnet",
			Cloud:      AWS,
			ParentID:   aws.ToString(subnet.VpcId),
			Attributes: map[string]interface{}{"cidr_block": aws.ToString(subnet.CidrBlock), "availability_zone": aws.ToString(subnet.AvailabilityZone), "tags": tags},
		})
	}

	return resources, nil
}

func (c *AWSClient) fetchSecurityGroups() ([]Resource, error) {
	var resources []Resource

	result, err := c.ec2.DescribeSecurityGroups(context.TODO(), &ec2.DescribeSecurityGroupsInput{})
	if err != nil {
		return nil, err
	}

	for _, sg := range result.SecurityGroups {
		tags := make(map[string]string)
		for _, tag := range sg.Tags {
			tags[*tag.Key] = *tag.Value
		}

		resources = append(resources, Resource{
			ID:         aws.ToString(sg.GroupId),
			Name:       aws.ToString(sg.GroupName),
			Type:       "security_group",
			Cloud:      AWS,
			ParentID:   aws.ToString(sg.VpcId),
			Attributes: map[string]interface{}{"description": aws.ToString(sg.Description), "tags": tags},
		})
	}

	return resources, nil
}

func (c *AWSClient) fetchInstances() ([]Resource, error) {
	var resources []Resource

	result, err := c.ec2.DescribeInstances(context.TODO(), &ec2.DescribeInstancesInput{})
	if err != nil {
		return nil, err
	}

	for _, reservation := range result.Reservations {
		for _, instance := range reservation.Instances {
			tags := make(map[string]string)
			for _, tag := range instance.Tags {
				tags[*tag.Key] = *tag.Value
			}

			resources = append(resources, Resource{
				ID:         aws.ToString(instance.InstanceId),
				Name:       tags["Name"],
				Type:       "instance",
				Cloud:      AWS,
				ParentID:   aws.ToString(instance.SubnetId),
				Attributes: map[string]interface{}{"instance_type": aws.ToString(instance.InstanceType), "ami": aws.ToString(instance.ImageId), "tags": tags},
			})
		}
	}

	return resources, nil
}