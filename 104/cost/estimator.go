package cost

import (
	"encoding/json"
	"fmt"
	"io/ioutil"
	"net/http"
	"strconv"
	"strings"

	"terraform-config-generator/cloud"
)

type CostEstimate struct {
	Resources      []ResourceCost
	TotalMonthly   float64
	TotalYearly    float64
	Currency       string
	Optimizations  []OptimizationSuggestion
	BudgetLimit    float64
	OverBudget     bool
}

type ResourceCost struct {
	ID             string
	Name           string
	Type           string
	Cloud          cloud.CloudType
	Region         string
	InstanceType   string
	MonthlyCost    float64
	YearlyCost     float64
	Unit           string
	Details        []CostDetail
}

type CostDetail struct {
	Component  string
	MonthlyCost float64
	YearlyCost  float64
	Unit        string
}

type OptimizationSuggestion struct {
	ResourceID     string
	ResourceName   string
	CurrentCost    float64
	SuggestedCost  float64
	SavingsPercent float64
	Description    string
	Recommendation string
	Priority       string
}

type Estimator struct {
	BudgetLimit float64
	Currency    string
	UseSP       bool
	UseSpot     bool
}

func NewEstimator(budgetLimit float64, useSP, useSpot bool) *Estimator {
	return &Estimator{
		BudgetLimit: budgetLimit,
		Currency:    "USD",
		UseSP:       useSP,
		UseSpot:     useSpot,
	}
}

func (e *Estimator) Estimate(resources []cloud.Resource) (*CostEstimate, error) {
	estimate := &CostEstimate{
		Resources:    []ResourceCost{},
		Optimizations: []OptimizationSuggestion{},
		BudgetLimit:  e.BudgetLimit,
		Currency:     e.Currency,
	}

	for _, resource := range resources {
		cost, err := e.calculateResourceCost(resource)
		if err != nil {
			return nil, fmt.Errorf("计算资源 %s 成本失败: %w", resource.ID, err)
		}
		estimate.Resources = append(estimate.Resources, cost)
		estimate.TotalMonthly += cost.MonthlyCost
		estimate.TotalYearly += cost.YearlyCost
	}

	estimate.Optimizations = e.generateOptimizations(estimate.Resources)
	estimate.OverBudget = e.BudgetLimit > 0 && estimate.TotalMonthly > e.BudgetLimit

	return estimate, nil
}

func (e *Estimator) calculateResourceCost(resource cloud.Resource) (ResourceCost, error) {
	cost := ResourceCost{
		ID:         resource.ID,
		Name:       resource.Name,
		Type:       resource.Type,
		Cloud:      resource.Cloud,
		Region:     resource.Attributes["region"].(string),
		InstanceType: getInstanceType(resource),
		Details:    []CostDetail{},
	}

	var monthlyCost float64
	var err error

	switch resource.Cloud {
	case cloud.AWS:
		monthlyCost, err = e.getAWSPrice(resource)
	case cloud.Aliyun:
		monthlyCost, err = e.getAliyunPrice(resource)
	case cloud.Tencent:
		monthlyCost, err = e.getTencentPrice(resource)
	}

	if err != nil {
		return cost, err
	}

	if e.UseSP {
		monthlyCost *= 0.7
	}
	if e.UseSpot {
		monthlyCost *= 0.3
	}

	cost.MonthlyCost = monthlyCost
	cost.YearlyCost = monthlyCost * 12
	cost.Unit = "hours"

	return cost, nil
}

func (e *Estimator) getAWSPrice(resource cloud.Resource) (float64, error) {
	switch resource.Type {
	case "instance":
		instanceType := resource.Attributes["instance_type"].(string)
		return e.fetchAWSEstimate(instanceType, resource.Attributes["region"].(string))
	case "vpc":
		return 0, nil
	case "subnet":
		return 0, nil
	case "security_group":
		return 0, nil
	case "slb":
		return e.getAWSLoadBalancerPrice(resource)
	default:
		return 0, nil
	}
}

func (e *Estimator) fetchAWSEstimate(instanceType, region string) (float64, error) {
	priceMap := map[string]map[string]float64{
		"us-east-1": {
			"t3.micro":   8.64,
			"t3.small":  17.28,
			"t3.medium": 34.56,
			"t3.large":  69.12,
			"t3.xlarge": 138.24,
			"c5.large":  84.96,
			"c5.xlarge": 169.92,
			"m5.large":  75.60,
			"m5.xlarge": 151.20,
		},
		"ap-east-1": {
			"t3.micro":   9.50,
			"t3.small":  19.00,
			"t3.medium": 38.00,
			"t3.large":  76.00,
		},
	}

	if regionPrices, exists := priceMap[region]; exists {
		if price, exists := regionPrices[instanceType]; exists {
			return price, nil
		}
	}

	return 100, nil
}

func (e *Estimator) getAWSLoadBalancerPrice(resource cloud.Resource) (float64, error) {
	return 20, nil
}

func (e *Estimator) getAliyunPrice(resource cloud.Resource) (float64, error) {
	switch resource.Type {
	case "instance":
		instanceType := resource.Attributes["instance_type"].(string)
		return e.fetchAliyunEstimate(instanceType, resource.Attributes["region"].(string))
	case "vpc":
		return 0, nil
	case "subnet":
		return 0, nil
	case "security_group":
		return 0, nil
	case "slb":
		return e.getAliyunLoadBalancerPrice(resource)
	default:
		return 0, nil
	}
}

func (e *Estimator) fetchAliyunEstimate(instanceType, region string) (float64, error) {
	priceMap := map[string]map[string]float64{
		"cn-hangzhou": {
			"ecs.g6.large":   58.00,
			"ecs.g6.xlarge":  116.00,
			"ecs.c6.large":   68.00,
			"ecs.c6.xlarge":  136.00,
			"ecs.r6.large":   85.00,
			"ecs.r6.xlarge":  170.00,
			"ecs.t6.large":   28.00,
			"ecs.t6.xlarge":  56.00,
		},
		"cn-beijing": {
			"ecs.g6.large": 60.00,
			"ecs.t6.large": 30.00,
		},
	}

	if regionPrices, exists := priceMap[region]; exists {
		if price, exists := regionPrices[instanceType]; exists {
			return price, nil
		}
	}

	return 80, nil
}

func (e *Estimator) getAliyunLoadBalancerPrice(resource cloud.Resource) (float64, error) {
	return 15, nil
}

func (e *Estimator) getTencentPrice(resource cloud.Resource) (float64, error) {
	switch resource.Type {
	case "instance":
		instanceType := resource.Attributes["instance_type"].(string)
		return e.fetchTencentEstimate(instanceType, resource.Attributes["region"].(string))
	case "vpc":
		return 0, nil
	case "subnet":
		return 0, nil
	case "security_group":
		return 0, nil
	case "slb":
		return e.getTencentLoadBalancerPrice(resource)
	default:
		return 0, nil
	}
}

func (e *Estimator) fetchTencentEstimate(instanceType, region string) (float64, error) {
	priceMap := map[string]map[string]float64{
		"ap-beijing": {
			"S5.LARGE8":   52.00,
			"S5.XLARGE16": 104.00,
			"C5.LARGE8":   62.00,
			"C5.XLARGE16": 124.00,
			"M5.LARGE8":   58.00,
			"M5.XLARGE16": 116.00,
		},
		"ap-hongkong": {
			"S5.LARGE8": 55.00,
		},
	}

	if regionPrices, exists := priceMap[region]; exists {
		if price, exists := regionPrices[instanceType]; exists {
			return price, nil
		}
	}

	return 70, nil
}

func (e *Estimator) getTencentLoadBalancerPrice(resource cloud.Resource) (float64, error) {
	return 18, nil
}

func getInstanceType(resource cloud.Resource) string {
	if instanceType, ok := resource.Attributes["instance_type"]; ok {
		return instanceType.(string)
	}
	return ""
}

func (e *Estimator) generateOptimizations(resources []ResourceCost) []OptimizationSuggestion {
	var suggestions []OptimizationSuggestion

	for _, resource := range resources {
		if resource.Type == "instance" && resource.InstanceType != "" {
			suggestion := e.suggestInstanceOptimization(resource)
			if suggestion != nil {
				suggestions = append(suggestions, *suggestion)
			}

			spSuggestion := e.suggestSavingsPlans(resource)
			if spSuggestion != nil {
				suggestions = append(suggestions, *spSuggestion)
			}

			spotSuggestion := e.suggestSpotInstance(resource)
			if spotSuggestion != nil {
				suggestions = append(suggestions, *spotSuggestion)
			}
		}
	}

	return suggestions
}

func (e *Estimator) suggestInstanceOptimization(resource ResourceCost) *OptimizationSuggestion {
	sizeMap := map[string][]string{
		"t3":    {"nano", "micro", "small", "medium", "large", "xlarge", "2xlarge"},
		"t3a":   {"nano", "micro", "small", "medium", "large", "xlarge", "2xlarge"},
		"t6":    {"nano", "micro", "small", "medium", "large", "xlarge"},
		"g6":    {"small", "medium", "large", "xlarge", "2xlarge"},
		"c5":    {"large", "xlarge", "2xlarge", "4xlarge"},
		"c6":    {"large", "xlarge", "2xlarge"},
		"m5":    {"large", "xlarge", "2xlarge"},
		"S5":    {"SMALL4", "MEDIUM4", "LARGE8", "XLARGE16"},
		"C5":    {"LARGE8", "XLARGE16", "2XLARGE32"},
		"M5":    {"LARGE8", "XLARGE16"},
	}

	instanceType := resource.InstanceType
	family := ""
	size := ""

	for fam, sizes := range sizeMap {
		if strings.HasPrefix(instanceType, fam) {
			family = fam
			for _, s := range sizes {
				if strings.HasSuffix(instanceType, s) {
					size = s
					break
				}
			}
			break
		}
	}

	if family == "" || size == "" {
		return nil
	}

	sizes := sizeMap[family]
	currentIdx := -1
	for i, s := range sizes {
		if s == size {
			currentIdx = i
			break
		}
	}

	if currentIdx <= 0 {
		return nil
	}

	smallerSize := sizes[currentIdx-1]
	smallerType := strings.Replace(instanceType, size, smallerSize, 1)

	savingsPercent := 40.0
	suggestedCost := resource.MonthlyCost * (1 - savingsPercent/100)

	return &OptimizationSuggestion{
		ResourceID:     resource.ID,
		ResourceName:   resource.Name,
		CurrentCost:    resource.MonthlyCost,
		SuggestedCost:  suggestedCost,
		SavingsPercent: savingsPercent,
		Description:    fmt.Sprintf("将实例类型从 %s 降级到 %s", instanceType, smallerType),
		Recommendation: fmt.Sprintf("将 %s (%s) 的实例类型从 %s 降到 %s 可节省约 %.1f%% 的成本",
			resource.Name, resource.ID, instanceType, smallerType, savingsPercent),
		Priority: "high",
	}
}

func (e *Estimator) suggestSavingsPlans(resource ResourceCost) *OptimizationSuggestion {
	if e.UseSP {
		return nil
	}

	savingsPercent := 30.0
	suggestedCost := resource.MonthlyCost * (1 - savingsPercent/100)

	return &OptimizationSuggestion{
		ResourceID:     resource.ID,
		ResourceName:   resource.Name,
		CurrentCost:    resource.MonthlyCost,
		SuggestedCost:  suggestedCost,
		SavingsPercent: savingsPercent,
		Description:    "使用AWS Savings Plans或阿里云预留实例",
		Recommendation: fmt.Sprintf("为 %s (%s) 购买Savings Plans/预留实例可节省约 %.1f%% 的成本",
			resource.Name, resource.ID, savingsPercent),
		Priority: "medium",
	}
}

func (e *Estimator) suggestSpotInstance(resource ResourceCost) *OptimizationSuggestion {
	if e.UseSpot {
		return nil
	}

	if strings.HasPrefix(resource.InstanceType, "t3") ||
		strings.HasPrefix(resource.InstanceType, "t6") ||
		strings.HasPrefix(resource.InstanceType, "S5") {
		savingsPercent := 70.0
		suggestedCost := resource.MonthlyCost * (1 - savingsPercent/100)

		return &OptimizationSuggestion{
			ResourceID:     resource.ID,
			ResourceName:   resource.Name,
			CurrentCost:    resource.MonthlyCost,
			SuggestedCost:  suggestedCost,
			SavingsPercent: savingsPercent,
			Description:    "使用Spot实例",
			Recommendation: fmt.Sprintf("为 %s (%s) 使用Spot实例可节省约 %.1f%% 的成本（适合容错场景）",
				resource.Name, resource.ID, savingsPercent),
			Priority: "low",
		}
	}

	return nil
}

func FetchAWSPricing(instanceType, region string) (float64, error) {
	url := fmt.Sprintf("https://api.pricing.us-east-1.amazonaws.com/offers/v1.0/aws/AmazonEC2/current/%s/index.json", region)
	resp, err := http.Get(url)
	if err != nil {
		return 0, err
	}
	defer resp.Body.Close()

	body, err := ioutil.ReadAll(resp.Body)
	if err != nil {
		return 0, err
	}

	var data map[string]interface{}
	if err := json.Unmarshal(body, &data); err != nil {
		return 0, err
	}

	if products, ok := data["products"].(map[string]interface{}); ok {
		for _, product := range products {
			if p, ok := product.(map[string]interface{}); ok {
				if attrs, ok := p["attributes"].(map[string]interface{}); ok {
					if instanceTypeAttr, ok := attrs["instanceType"].(string); ok {
						if instanceTypeAttr == instanceType {
							if sku, ok := p["sku"].(string); ok {
								if terms, ok := data["terms"].(map[string]interface{}); ok {
									if onDemand, ok := terms["OnDemand"].(map[string]interface{}); ok {
										if term, ok := onDemand[sku].(map[string]interface{}); ok {
											for _, price := range term {
												if pr, ok := price.(map[string]interface{}); ok {
													if priceDimensions, ok := pr["priceDimensions"].(map[string]interface{}); ok {
														for _, pd := range priceDimensions {
															if pdim, ok := pd.(map[string]interface{}); ok {
																if pricePerUnit, ok := pdim["pricePerUnit"].(map[string]interface{}); ok {
																	if usd, ok := pricePerUnit["USD"].(string); ok {
																		return strconv.ParseFloat(usd)
																	}
																}
															}
														}
													}
												}
											}
										}
									}
								}
							}
						}
					}
				}
			}
		}
	}

	return 0, nil
}