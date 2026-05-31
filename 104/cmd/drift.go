package cmd

import (
	"fmt"

	"github.com/spf13/cobra"

	"terraform-config-generator/cloud"
	"terraform-config-generator/drift"
)

func NewDriftCommand() *cobra.Command {
	cmd := &cobra.Command{
		Use:   "drift",
		Short: "检测资源偏差",
		Long:  `检测实际云资源与Terraform代码之间的偏差`,
		RunE: func(cmd *cobra.Command, args []string) error {
			return runDrift()
		},
	}

	return cmd
}

func runDrift() error {
	clients, err := cloud.NewCloudClients(cloud)
	if err != nil {
		return fmt.Errorf("创建云客户端失败: %w", err)
	}

	detector := drift.NewDetector(outputDir, clients)
	drifts, err := detector.Detect()
	if err != nil {
		return fmt.Errorf("检测偏差失败: %w", err)
	}

	if len(drifts) == 0 {
		fmt.Println("未检测到资源偏差")
		return nil
	}

	fmt.Println("检测到以下资源偏差:")
	for _, d := range drifts {
		fmt.Printf("[%s] %s: %s\n", d.Type, d.ResourceID, d.Description)
	}

	if err := detector.GeneratePatch(drifts); err != nil {
		return fmt.Errorf("生成补丁失败: %w", err)
	}

	fmt.Println("补丁文件已生成")
	return nil
}