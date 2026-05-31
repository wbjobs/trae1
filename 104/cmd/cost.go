package cmd

import (
	"fmt"

	"github.com/spf13/cobra"

	"terraform-config-generator/cloud"
	"terraform-config-generator/cost"
)

var (
	budgetLimit  float64
	useSP        bool
	useSpot      bool
	filterRegion string
	filterType   string
	filterCloud  string
)

func NewCostCommand() *cobra.Command {
	cmd := &cobra.Command{
		Use:   "cost",
		Short: "成本估算",
		Long:  `计算云资源的月度和年度成本，并提供优化建议`,
		RunE: func(cmd *cobra.Command, args []string) error {
			return runCost()
		},
	}

	cmd.Flags().Float64Var(&budgetLimit, "budget-limit", 0, "预算上限（美元/月）")
	cmd.Flags().BoolVar(&useSP, "savings-plans", false, "考虑Savings Plans折扣")
	cmd.Flags().BoolVar(&useSpot, "spot", false, "考虑Spot实例折扣")
	cmd.Flags().StringVar(&filterRegion, "region", "", "按地域筛选")
	cmd.Flags().StringVar(&filterType, "instance-type", "", "按实例类型筛选")
	cmd.Flags().StringVar(&filterCloud, "filter-cloud", "", "按云服务商筛选")

	return cmd
}

func runCost() error {
	clients, err := cloud.NewCloudClients(cloud)
	if err != nil {
		return fmt.Errorf("创建云客户端失败: %w", err)
	}

	resources, err := cloud.FetchAllResources(clients)
	if err != nil {
		return fmt.Errorf("获取资源失败: %w", err)
	}

	estimator := cost.NewEstimator(budgetLimit, useSP, useSpot)

	if filterRegion != "" {
		resources = estimator.FilterByRegion(resources, filterRegion)
	}

	if filterType != "" {
		resources = estimator.FilterByInstanceType(resources, filterType)
	}

	if filterCloud != "" {
		resources = estimator.FilterByCloud(resources, cloud.CloudType(filterCloud))
	}

	estimate, err := estimator.Estimate(resources)
	if err != nil {
		return fmt.Errorf("成本估算失败: %w", err)
	}

	fmt.Println("=== 成本估算结果 ===")
	fmt.Printf("总月度费用: $%.2f\n", estimate.TotalMonthly)
	fmt.Printf("总年度费用: $%.2f\n", estimate.TotalYearly)

	if estimate.BudgetLimit > 0 {
		fmt.Printf("\n预算检查:\n")
		fmt.Printf("  预算上限: $%.2f\n", estimate.BudgetLimit)
		fmt.Printf("  当前费用: $%.2f\n", estimate.TotalMonthly)
		if estimate.OverBudget {
			fmt.Printf("  ⚠️ 警告: 费用超出预算！\n")
			return fmt.Errorf("费用 ($%.2f) 超出预算上限 ($%.2f)", estimate.TotalMonthly, estimate.BudgetLimit)
		} else {
			fmt.Printf("  ✓ 在预算内\n")
		}
	}

	if len(estimate.Optimizations) > 0 {
		fmt.Println("\n优化建议:")
		for _, opt := range estimate.Optimizations {
			fmt.Printf("  [%s] %s\n", opt.Priority, opt.Recommendation)
			fmt.Printf("    预计节省: $%.2f (%.1f%%)\n", opt.CurrentCost-opt.SuggestedCost, opt.SavingsPercent)
		}
	}

	if err := cost.GenerateReport(estimate, outputDir); err != nil {
		return fmt.Errorf("生成报告失败: %w", err)
	}

	fmt.Printf("\n成本报告已生成到: %s/cost_report.md\n", outputDir)

	return nil
}