package cmd

import (
	"fmt"

	"github.com/spf13/cobra"

	"terraform-config-generator/cloud"
	"terraform-config-generator/graph"
)

func NewGraphCommand() *cobra.Command {
	cmd := &cobra.Command{
		Use:   "graph",
		Short: "生成依赖关系图",
		Long:  `生成云资源之间的依赖关系图`,
		RunE: func(cmd *cobra.Command, args []string) error {
			return runGraph()
		},
	}

	return cmd
}

func runGraph() error {
	clients, err := cloud.NewCloudClients(cloud)
	if err != nil {
		return fmt.Errorf("创建云客户端失败: %w", err)
	}

	resources, err := cloud.FetchAllResources(clients)
	if err != nil {
		return fmt.Errorf("获取资源失败: %w", err)
	}

	graphBuilder := graph.NewGraphBuilder()
	depGraph := graphBuilder.BuildDependencyGraph(resources)

	if err := depGraph.ExportDOT(outputDir); err != nil {
		return fmt.Errorf("导出DOT文件失败: %w", err)
	}

	if err := depGraph.ExportMermaid(outputDir); err != nil {
		return fmt.Errorf("导出Mermaid文件失败: %w", err)
	}

	fmt.Printf("依赖关系图已导出到目录: %s\n", outputDir)
	return nil
}