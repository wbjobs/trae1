package cost

import (
	"fmt"
	"os"
	"path/filepath"
	"sort"
	"strings"
)

func GenerateReport(estimate *CostEstimate, outputDir string) error {
	filePath := filepath.Join(outputDir, "cost_report.md")

	f, err := os.Create(filePath)
	if err != nil {
		return fmt.Errorf("创建成本报告失败: %w", err)
	}
	defer f.Close()

	fmt.Fprintln(f, "# 云资源成本估算报告")
	fmt.Fprintln(f)

	fmt.Fprintln(f, "## 概览")
	fmt.Fprintln(f)
	fmt.Fprintf(f, "- **总月度费用**: $%.2f\n", estimate.TotalMonthly)
	fmt.Fprintf(f, "- **总年度费用**: $%.2f\n", estimate.TotalYearly)
	fmt.Fprintf(f, "- **货币单位**: %s\n", estimate.Currency)
	fmt.Fprintln(f)

	if estimate.BudgetLimit > 0 {
		fmt.Fprintln(f, "## 预算检查")
		fmt.Fprintln(f)
		fmt.Fprintf(f, "- **预算上限**: $%.2f\n", estimate.BudgetLimit)
		fmt.Fprintf(f, "- **当前费用**: $%.2f\n", estimate.TotalMonthly)
		fmt.Fprintf(f, "- **状态**: %s\n", getBudgetStatus(estimate.OverBudget))
		fmt.Fprintln(f)
	}

	fmt.Fprintln(f, "## 资源成本明细")
	fmt.Fprintln(f)

	fmt.Fprintln(f, "### 按云服务商分组")
	fmt.Fprintln(f)

	cloudGroups := groupByCloud(estimate.Resources)
	for cloud, resources := range cloudGroups {
		fmt.Fprintf(f, "#### %s\n", cloud)
		fmt.Fprintln(f)

		totalCost := 0.0
		for _, r := range resources {
			totalCost += r.MonthlyCost
		}

		fmt.Fprintln(f, "| 资源名称 | 类型 | 实例类型 | 月度费用 |")
		fmt.Fprintln(f, "|----------|------|----------|----------|")
		for _, r := range resources {
			fmt.Fprintf(f, "| %s | %s | %s | $%.2f |\n", r.Name, r.Type, r.InstanceType, r.MonthlyCost)
		}
		fmt.Fprintf(f, "| **合计** | | | **$%.2f** |\n", totalCost)
		fmt.Fprintln(f)
	}

	fmt.Fprintln(f, "### 按地域分组")
	fmt.Fprintln(f)

	regionGroups := groupByRegion(estimate.Resources)
	for region, resources := range regionGroups {
		fmt.Fprintf(f, "#### %s\n", region)
		fmt.Fprintln(f)

		totalCost := 0.0
		for _, r := range resources {
			totalCost += r.MonthlyCost
		}

		fmt.Fprintf(f, "**月度费用**: $%.2f\n", totalCost)
		fmt.Fprintln(f)
	}

	fmt.Fprintln(f, "## 成本优化建议")
	fmt.Fprintln(f)

	if len(estimate.Optimizations) == 0 {
		fmt.Fprintln(f, "暂无优化建议")
		fmt.Fprintln(f)
		return nil
	}

	sortedOptimizations := sortOptimizations(estimate.Optimizations)

	highPriority := filterByPriority(sortedOptimizations, "high")
	mediumPriority := filterByPriority(sortedOptimizations, "medium")
	lowPriority := filterByPriority(sortedOptimizations, "low")

	if len(highPriority) > 0 {
		fmt.Fprintln(f, "### 高优先级")
		fmt.Fprintln(f)
		for _, opt := range highPriority {
			fmt.Fprintf(f, "- [%s] **%s**\n", opt.Priority, opt.Recommendation)
			fmt.Fprintf(f, "  - 当前费用: $%.2f -> 建议费用: $%.2f\n", opt.CurrentCost, opt.SuggestedCost)
			fmt.Fprintf(f, "  - 预计节省: %.1f%%\n", opt.SavingsPercent)
			fmt.Fprintln(f)
		}
	}

	if len(mediumPriority) > 0 {
		fmt.Fprintln(f, "### 中优先级")
		fmt.Fprintln(f)
		for _, opt := range mediumPriority {
			fmt.Fprintf(f, "- [%s] **%s**\n", opt.Priority, opt.Recommendation)
			fmt.Fprintf(f, "  - 当前费用: $%.2f -> 建议费用: $%.2f\n", opt.CurrentCost, opt.SuggestedCost)
			fmt.Fprintf(f, "  - 预计节省: %.1f%%\n", opt.SavingsPercent)
			fmt.Fprintln(f)
		}
	}

	if len(lowPriority) > 0 {
		fmt.Fprintln(f, "### 低优先级")
		fmt.Fprintln(f)
		for _, opt := range lowPriority {
			fmt.Fprintf(f, "- [%s] **%s**\n", opt.Priority, opt.Recommendation)
			fmt.Fprintf(f, "  - 当前费用: $%.2f -> 建议费用: $%.2f\n", opt.CurrentCost, opt.SuggestedCost)
			fmt.Fprintf(f, "  - 预计节省: %.1f%%\n", opt.SavingsPercent)
			fmt.Fprintln(f)
		}
	}

	fmt.Fprintln(f, "## 预估节省汇总")
	fmt.Fprintln(f)

	totalSavings := calculateTotalSavings(estimate.Optimizations)
	fmt.Fprintf(f, "- **预计可节省**: $%.2f/月 (约 %.1f%%)\n", totalSavings, (totalSavings/estimate.TotalMonthly)*100)
	fmt.Fprintf(f, "- **优化后费用**: $%.2f/月\n", estimate.TotalMonthly-totalSavings)
	fmt.Fprintln(f)

	return nil
}

func groupByCloud(resources []ResourceCost) map[string][]ResourceCost {
	groups := make(map[string][]ResourceCost)
	for _, r := range resources {
		key := strings.Title(string(r.Cloud))
		groups[key] = append(groups[key], r)
	}
	return groups
}

func groupByRegion(resources []ResourceCost) map[string][]ResourceCost {
	groups := make(map[string][]ResourceCost)
	for _, r := range resources {
		groups[r.Region] = append(groups[r.Region], r)
	}
	return groups
}

func sortOptimizations(optimizations []OptimizationSuggestion) []OptimizationSuggestion {
	sorted := make([]OptimizationSuggestion, len(optimizations))
	copy(sorted, optimizations)

	sort.Slice(sorted, func(i, j int) bool {
		priorityOrder := map[string]int{"high": 0, "medium": 1, "low": 2}
		if priorityOrder[sorted[i].Priority] != priorityOrder[sorted[j].Priority] {
			return priorityOrder[sorted[i].Priority] < priorityOrder[sorted[j].Priority]
		}
		return sorted[i].SavingsPercent > sorted[j].SavingsPercent
	})

	return sorted
}

func filterByPriority(optimizations []OptimizationSuggestion, priority string) []OptimizationSuggestion {
	var filtered []OptimizationSuggestion
	for _, opt := range optimizations {
		if opt.Priority == priority {
			filtered = append(filtered, opt)
		}
	}
	return filtered
}

func calculateTotalSavings(optimizations []OptimizationSuggestion) float64 {
	total := 0.0
	for _, opt := range optimizations {
		total += opt.CurrentCost - opt.SuggestedCost
	}
	return total
}

func getBudgetStatus(overBudget bool) string {
	if overBudget {
		return ":red_circle: 超出预算"
	}
	return ":green_circle: 在预算内"
}

func (e *Estimator) FilterByRegion(resources []cloud.Resource, region string) []cloud.Resource {
	var filtered []cloud.Resource
	for _, r := range resources {
		if r.Attributes["region"] == region {
			filtered = append(filtered, r)
		}
	}
	return filtered
}

func (e *Estimator) FilterByInstanceType(resources []cloud.Resource, instanceType string) []cloud.Resource {
	var filtered []cloud.Resource
	for _, r := range resources {
		if r.Type == "instance" && r.Attributes["instance_type"] == instanceType {
			filtered = append(filtered, r)
		}
	}
	return filtered
}

func (e *Estimator) FilterByCloud(resources []cloud.Resource, cloud cloud.CloudType) []cloud.Resource {
	var filtered []cloud.Resource
	for _, r := range resources {
		if r.Cloud == cloud {
			filtered = append(filtered, r)
		}
	}
	return filtered
}