package cmd

import (
	"fmt"

	"github.com/spf13/cobra"

	"terraform-config-generator/cloud"
	"terraform-config-generator/cost"
	"terraform-config-generator/generator"
	"terraform-config-generator/graph"
)

func NewGenerateCommand() *cobra.Command {
	cmd := &cobra.Command{
		Use:   "generate",
		Short: "生成Terraform配置",
		Long:  `从云API获取资源信息并生成Terraform HCL配置`,
		RunE: func(cmd *cobra.Command, args []string) error {
			return runGenerate()
		},
	}

	cmd.Flags().Float64Var(&budgetLimit, "budget-limit", 0, "预算上限（美元/月）")
	cmd.Flags().BoolVar(&useSP, "savings-plans", false, "考虑Savings Plans折扣")
	cmd.Flags().BoolVar(&useSpot, "spot", false, "考虑Spot实例折扣")

	return cmd
}

func runGenerate() error {
	clients, err := cloud.NewCloudClients(cloud)
	if err != nil {
		return fmt.Errorf("创建云客户端失败: %w", err)
	}

	resources, err := cloud.FetchAllResources(clients)
	if err != nil {
		return fmt.Errorf("获取资源失败: %w", err)
	}

	if budgetLimit > 0 {
		estimator := cost.NewEstimator(budgetLimit, useSP, useSpot)
		estimate, err := estimator.Estimate(resources)
		if err != nil {
			return fmt.Errorf("成本估算失败: %w", err)
		}

		fmt.Printf("成本预估算: $%.2f/月\n", estimate.TotalMonthly)
		
		if estimate.OverBudget {
			return fmt.Errorf("❌ 费用 ($%.2f) 超出预算上限 ($%.2f)，拒绝生成代码", estimate.TotalMonthly, budgetLimit)
		}

		fmt.Printf("✓ 费用在预算范围内\n")
		
		if err := cost.GenerateReport(estimate, outputDir); err != nil {
			return fmt.Errorf("生成成本报告失败: %w", err)
		}
	}

	graphBuilder := graph.NewGraphBuilder()
	depGraph := graphBuilder.BuildDependencyGraph(resources)

	result := depGraph.DetectCycles()
	if !result.IsDAG {
		fmt.Println("⚠️ 检测到循环依赖，正在处理...")
		
		if interactive {
			editor := graph.NewInteractiveEditor(depGraph, resources)
			if err := editor.Run(); err != nil {
				return fmt.Errorf("交互式编辑失败: %w", err)
			}
		} else {
			adjustments := depGraph.BreakCycles(result.Cycles)
			depGraph.ApplyAdjustments(adjustments)
			fmt.Printf("已自动处理 %d 处循环依赖\n", len(adjustments))
		}
	}

	gen := generator.NewGenerator(outputDir, format)
	gen.SetDependencyGraph(depGraph)
	
	if !result.IsDAG {
		adjustments := depGraph.BreakCycles(result.Cycles)
		gen.SetAdjustments(adjustments)
	}
	
	if err := gen.Generate(resources, importMode); err != nil {
		return fmt.Errorf("生成配置失败: %w", err)
	}

	fmt.Printf("Terraform配置已生成到目录: %s\n", outputDir)
	return nil
}