package cmd

import (
	"fmt"

	"github.com/spf13/cobra"

	"terraform-config-generator/cloud"
	"terraform-config-generator/graph"
)

func NewCycleCommand() *cobra.Command {
	cmd := &cobra.Command{
		Use:   "cycle",
		Short: "检测和处理循环依赖",
		Long:  `使用Tarjan算法检测资源间的循环依赖，并提供破环建议`,
		RunE: func(cmd *cobra.Command, args []string) error {
			return runCycle()
		},
	}

	return cmd
}

func runCycle() error {
	clients, err := cloud.NewCloudClients(cloudType)
	if err != nil {
		return fmt.Errorf("创建云客户端失败: %w", err)
	}

	resources, err := cloud.FetchAllResources(clients)
	if err != nil {
		return fmt.Errorf("获取资源失败: %w", err)
	}

	graphBuilder := graph.NewGraphBuilder()
	depGraph := graphBuilder.BuildDependencyGraph(resources)

	result := depGraph.DetectCycles()

	if result.IsDAG {
		fmt.Println("✓ 图是有向无环图(DAG)，没有循环依赖")
		return nil
	}

	fmt.Println("⚠️ 检测到循环依赖:")
	for _, suggestion := range result.Suggestions {
		fmt.Println("  -", suggestion)
	}

	if err := depGraph.ExportCycleDOT(outputDir, result.Cycles); err != nil {
		return fmt.Errorf("导出循环检测DOT文件失败: %w", err)
	}

	fmt.Printf("\n循环依赖可视化图已导出到: %s/cycle_detection.dot\n", outputDir)

	if interactive {
		editor := graph.NewInteractiveEditor(depGraph, resources)
		if err := editor.Run(); err != nil {
			return fmt.Errorf("交互式编辑失败: %w", err)
		}
	} else {
		adjustments := depGraph.BreakCycles(result.Cycles)
		fmt.Printf("\n自动破环建议 (%d 处调整):\n", len(adjustments))
		for _, adj := range adjustments {
			fmt.Printf("  - 移除依赖: %s -> %s (%s)\n", adj.From, adj.To, adj.Reason)
			fmt.Printf("    建议: %s\n", adj.Suggestion)
		}

		depGraph.ApplyAdjustments(adjustments)
		result = depGraph.DetectCycles()
		if result.IsDAG {
			fmt.Println("\n✓ 自动破环成功，图现在是DAG")
		}
	}

	return nil
}