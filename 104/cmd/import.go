package cmd

import (
	"fmt"
	"os"
	"path/filepath"
	"regexp"

	"github.com/spf13/cobra"

	"terraform-config-generator/cloud"
)

type importMapping struct {
	ResourceType string
	ResourceID   string
	TerraformID  string
}

func NewImportCommand() *cobra.Command {
	cmd := &cobra.Command{
		Use:   "import",
		Short: "生成Import配置",
		Long:  `生成Terraform import配置文件，用于将已有资源导入Terraform管理`,
		RunE: func(cmd *cobra.Command, args []string) error {
			return runImport()
		},
	}

	return cmd
}

func runImport() error {
	clients, err := cloud.NewCloudClients(cloud)
	if err != nil {
		return fmt.Errorf("创建云客户端失败: %w", err)
	}

	resources, err := cloud.FetchAllResources(clients)
	if err != nil {
		return fmt.Errorf("获取资源失败: %w", err)
	}

	var imports []importMapping
	for _, resource := range resources {
		tfType := getTerraformResourceType(resource)
		if tfType == "" {
			continue
		}
		tfID := fmt.Sprintf("%s.%s", tfType, sanitizeName(resource.Name))
		imports = append(imports, importMapping{
			ResourceType: tfType,
			ResourceID:   resource.ID,
			TerraformID:  tfID,
		})
	}

	if err := writeImports(imports, outputDir); err != nil {
		return fmt.Errorf("写入导入配置失败: %w", err)
	}

	fmt.Println("Import配置已生成")
	return nil
}

func getTerraformResourceType(resource cloud.Resource) string {
	switch resource.Type {
	case "vpc":
		switch resource.Cloud {
		case cloud.AWS:
			return "aws_vpc"
		case cloud.Aliyun:
			return "alicloud_vpc"
		case cloud.Tencent:
			return "tencentcloud_vpc"
		}
	case "subnet":
		switch resource.Cloud {
		case cloud.AWS:
			return "aws_subnet"
		case cloud.Aliyun:
			return "alicloud_vswitch"
		case cloud.Tencent:
			return "tencentcloud_subnet"
		}
	case "security_group":
		switch resource.Cloud {
		case cloud.AWS:
			return "aws_security_group"
		case cloud.Aliyun:
			return "alicloud_security_group"
		case cloud.Tencent:
			return "tencentcloud_security_group"
		}
	case "instance":
		switch resource.Cloud {
		case cloud.AWS:
			return "aws_instance"
		case cloud.Aliyun:
			return "alicloud_instance"
		case cloud.Tencent:
			return "tencentcloud_instance"
		}
	case "slb":
		switch resource.Cloud {
		case cloud.AWS:
			return "aws_lb"
		case cloud.Aliyun:
			return "alicloud_slb"
		case cloud.Tencent:
			return "tencentcloud_clb"
		}
	}
	return ""
}

func sanitizeName(name string) string {
	re := regexp.MustCompile(`[^a-zA-Z0-9_]`)
	return re.ReplaceAllString(name, "_")
}

func writeImports(imports []importMapping, outputDir string) error {
	if err := os.MkdirAll(outputDir, 0755); err != nil {
		return err
	}

	importFile := filepath.Join(outputDir, "imports.tf")
	f, err := os.Create(importFile)
	if err != nil {
		return err
	}
	defer f.Close()

	for _, imp := range imports {
		fmt.Fprintf(f, "import {\n  to = %s\n  id = %q\n}\n\n", imp.TerraformID, imp.ResourceID)
	}

	return nil
}