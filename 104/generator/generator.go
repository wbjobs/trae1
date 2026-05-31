package generator

import (
	"fmt"
	"os"
	"path/filepath"
	"regexp"
	"sort"
	"strings"

	"github.com/hashicorp/hcl/v2/hclwrite"
	"github.com/zclconf/go-cty/cty"

	"terraform-config-generator/cloud"
	"terraform-config-generator/graph"
)

type Generator struct {
	outputDir   string
	format      bool
	graph       *graph.DependencyGraph
	adjustments []graph.DependencyAdjustment
}

func NewGenerator(outputDir string, format bool) *Generator {
	return &Generator{
		outputDir: outputDir,
		format:    format,
	}
}

func (g *Generator) SetDependencyGraph(dg *graph.DependencyGraph) {
	g.graph = dg
}

func (g *Generator) SetAdjustments(adjustments []graph.DependencyAdjustment) {
	g.adjustments = adjustments
}

func (g *Generator) Generate(resources []cloud.Resource, importMode bool) error {
	if err := os.MkdirAll(g.outputDir, 0755); err != nil {
		return fmt.Errorf("创建输出目录失败: %w", err)
	}

	if g.graph != nil {
		if err := g.generateCycleDetectionReport(); err != nil {
			return fmt.Errorf("生成循环检测报告失败: %w", err)
		}
	}

	resourcesByType := g.groupResourcesByType(resources)

	sortedTypes, err := g.getSortedResourceTypes(resourcesByType)
	if err != nil {
		return fmt.Errorf("获取排序后的资源类型失败: %w", err)
	}

	for _, resourceType := range sortedTypes {
		items := resourcesByType[resourceType]
		if err := g.generateResourceFile(resourceType, items); err != nil {
			return fmt.Errorf("生成资源文件失败 %s: %w", resourceType, err)
		}
	}

	if err := g.generateVariables(resources); err != nil {
		return fmt.Errorf("生成变量文件失败: %w", err)
	}

	if err := g.generateOutputs(resources); err != nil {
		return fmt.Errorf("生成输出文件失败: %w", err)
	}

	if importMode {
		if err := g.generateImports(resources); err != nil {
			return fmt.Errorf("生成导入配置失败: %w", err)
		}
	}

	if len(g.adjustments) > 0 {
		if err := g.generateAdjustmentsFile(); err != nil {
			return fmt.Errorf("生成调整文件失败: %w", err)
		}
	}

	return nil
}

func (g *Generator) getSortedResourceTypes(resourcesByType map[string][]cloud.Resource) ([]string, error) {
	if g.graph == nil {
		types := make([]string, 0, len(resourcesByType))
		for t := range resourcesByType {
			types = append(types, t)
		}
		sort.Strings(types)
		return types, nil
	}

	sortedIDs, err := g.graph.TopologicalSort()
	if err != nil {
		types := make([]string, 0, len(resourcesByType))
		for t := range resourcesByType {
			types = append(types, t)
		}
		sort.Strings(types)
		return types, nil
	}

	typeSet := make(map[string]bool)
	var sortedTypes []string

	for _, id := range sortedIDs {
		if node, exists := g.graph.Nodes[id]; exists {
			key := fmt.Sprintf("%s_%s", node.Cloud, node.Type)
			if !typeSet[key] {
				typeSet[key] = true
				sortedTypes = append(sortedTypes, key)
			}
		}
	}

	for t := range resourcesByType {
		if !typeSet[t] {
			sortedTypes = append(sortedTypes, t)
		}
	}

	return sortedTypes, nil
}

func (g *Generator) generateCycleDetectionReport() error {
	result := g.graph.DetectCycles()
	if result.IsDAG {
		return nil
	}

	filePath := filepath.Join(g.outputDir, "cycle_report.md")
	f, err := os.Create(filePath)
	if err != nil {
		return err
	}
	defer f.Close()

	fmt.Fprintln(f, "# 循环依赖检测报告")
	fmt.Fprintln(f)
	fmt.Fprintln(f, "## 检测结果")
	fmt.Fprintln(f)
	fmt.Fprintf(f, "图中存在 **%d** 个循环依赖\n", len(result.Cycles))
	fmt.Fprintln(f)

	for i, cycle := range result.Cycles {
		fmt.Fprintf(f, "### 循环 %d\n", i+1)
		fmt.Fprintln(f)
		fmt.Fprintln(f, "```")
		nodeNames := make([]string, len(cycle))
		for j, id := range cycle {
			if node, exists := g.graph.Nodes[id]; exists {
				nodeNames[j] = fmt.Sprintf("%s (%s)", node.Name, node.Type)
			} else {
				nodeNames[j] = id
			}
		}
		fmt.Fprintln(f, strings.Join(nodeNames, " -> "))
		fmt.Fprintln(f, "```")
		fmt.Fprintln(f)
	}

	fmt.Fprintln(f, "## 建议")
	fmt.Fprintln(f)
	for _, suggestion := range result.Suggestions {
		fmt.Fprintf(f, "- %s\n", suggestion)
	}

	if err := g.graph.ExportCycleDOT(g.outputDir, result.Cycles); err != nil {
		return fmt.Errorf("导出循环检测DOT失败: %w", err)
	}

	return nil
}

func (g *Generator) generateAdjustmentsFile() error {
	filePath := filepath.Join(g.outputDir, "dependency_adjustments.tf")

	f := hclwrite.NewEmptyFile()
	rootBody := f.Body()

	rootBody.AppendNewBlock("comment", []string{"AUTOMATICALLY_GENERATED_DEPENDENCY_ADJUSTMENTS"})
	rootBody.AppendNewBlock("comment", []string{"This file contains dependency adjustments to break cycles"})

	for _, adj := range g.adjustments {
		if adj.Action == "remove" {
			if fromNode, exists := g.graph.Nodes[adj.From]; exists {
				if toNode, exists := g.graph.Nodes[adj.To]; exists {
					fromTfType := g.getTerraformType(fromNode.Resource)
					toTfType := g.getTerraformType(toNode.Resource)
					fromName := sanitizeName(fromNode.Name)
					toName := sanitizeName(toNode.Name)

					rootBody.AppendNewBlock("comment", []string{fmt.Sprintf("打破循环: %s -> %s", fromNode.Name, toNode.Name)})

					resourceBlock := rootBody.AppendNewBlock("resource", []string{fromTfType, fromName})
					resourceBody := resourceBlock.Body()
					resourceBody.SetAttributeValue("depends_on", cty.ListVal([]cty.Value{cty.StringVal(fmt.Sprintf("%s.%s", toTfType, toName))}))
				}
			}
		}
	}

	content := f.Bytes()
	if g.format {
		content = hclwrite.Format(content)
	}

	return os.WriteFile(filePath, content, 0644)
}

func (g *Generator) groupResourcesByType(resources []cloud.Resource) map[string][]cloud.Resource {
	result := make(map[string][]cloud.Resource)
	for _, r := range resources {
		key := fmt.Sprintf("%s_%s", r.Cloud, r.Type)
		result[key] = append(result[key], r)
	}
	return result
}

func (g *Generator) generateResourceFile(resourceType string, resources []cloud.Resource) error {
	filename := fmt.Sprintf("%s.tf", resourceType)
	filePath := filepath.Join(g.outputDir, filename)

	f := hclwrite.NewEmptyFile()
	rootBody := f.Body()

	for _, resource := range resources {
		tfType := g.getTerraformType(resource)
		tfName := sanitizeName(resource.Name)

		resourceBlock := rootBody.AppendNewBlock("resource", []string{tfType, tfName})
		resourceBody := resourceBlock.Body()

		for key, value := range resource.Attributes {
			if key == "tags" {
				continue
			}
			g.setValue(resourceBody, key, value)
		}

		if tags, ok := resource.Attributes["tags"]; ok {
			tagsMap := tags.(map[string]string)
			if len(tagsMap) > 0 {
				tagsBlock := resourceBody.AppendNewBlock("tags", nil)
				tagsBody := tagsBlock.Body()
				for k, v := range tagsMap {
					tagsBody.SetAttributeValue(k, cty.StringVal(v))
				}
			}
		}

		explicitDepends := g.getExplicitDependsOn(resource)
		if len(explicitDepends) > 0 {
			dependsVals := make([]cty.Value, len(explicitDepends))
			for i, dep := range explicitDepends {
				dependsVals[i] = cty.StringVal(dep)
			}
			resourceBody.SetAttributeValue("depends_on", cty.ListVal(dependsVals))
		}
	}

	content := f.Bytes()
	if g.format {
		content = hclwrite.Format(content)
	}

	return os.WriteFile(filePath, content, 0644)
}

func (g *Generator) getExplicitDependsOn(resource cloud.Resource) []string {
	var dependsOn []string

	if g.graph != nil {
		if edges, exists := g.graph.Edges[resource.ID]; exists {
			for _, targetID := range edges {
				if targetNode, exists := g.graph.Nodes[targetID]; exists {
					targetType := g.getTerraformType(targetNode.Resource)
					targetName := sanitizeName(targetNode.Name)
					dependsOn = append(dependsOn, fmt.Sprintf("%s.%s", targetType, targetName))
				}
			}
		}
	}

	if resource.ParentID != "" && g.graph != nil {
		if parentNode, exists := g.graph.Nodes[resource.ParentID]; exists {
			parentType := g.getTerraformType(parentNode.Resource)
			parentName := sanitizeName(parentNode.Name)
			parentRef := fmt.Sprintf("%s.%s", parentType, parentName)
			found := false
			for _, d := range dependsOn {
				if d == parentRef {
					found = true
					break
				}
			}
			if !found {
				dependsOn = append(dependsOn, parentRef)
			}
		}
	}

	return dependsOn
}

func (g *Generator) getTerraformType(resource cloud.Resource) string {
	switch resource.Cloud {
	case cloud.AWS:
		return fmt.Sprintf("aws_%s", resource.Type)
	case cloud.Aliyun:
		return fmt.Sprintf("alicloud_%s", resource.Type)
	case cloud.Tencent:
		return fmt.Sprintf("tencentcloud_%s", resource.Type)
	default:
		return fmt.Sprintf("%s_%s", resource.Cloud, resource.Type)
	}
}

func (g *Generator) setValue(body *hclwrite.Body, key string, value interface{}) {
	switch v := value.(type) {
	case string:
		body.SetAttributeValue(key, cty.StringVal(v))
	case int:
		body.SetAttributeValue(key, cty.NumberIntVal(int64(v)))
	case float64:
		body.SetAttributeValue(key, cty.NumberFloatVal(v))
	case bool:
		body.SetAttributeValue(key, cty.BoolVal(v))
	case []string:
		vals := make([]cty.Value, len(v))
		for i, s := range v {
			vals[i] = cty.StringVal(s)
		}
		body.SetAttributeValue(key, cty.ListVal(vals))
	default:
		body.SetAttributeValue(key, cty.StringVal(fmt.Sprintf("%v", value)))
	}
}

func (g *Generator) generateVariables(resources []cloud.Resource) error {
	filePath := filepath.Join(g.outputDir, "variables.tf")

	f := hclwrite.NewEmptyFile()
	rootBody := f.Body()

	varNames := make(map[string]bool)
	for _, r := range resources {
		cloudPrefix := string(r.Cloud)
		varNames[fmt.Sprintf("%s_region", cloudPrefix)] = true
		varNames[fmt.Sprintf("%s_access_key", cloudPrefix)] = true
		varNames[fmt.Sprintf("%s_secret_key", cloudPrefix)] = true
	}

	keys := make([]string, 0, len(varNames))
	for k := range varNames {
		keys = append(keys, k)
	}
	sort.Strings(keys)

	for _, name := range keys {
		varBlock := rootBody.AppendNewBlock("variable", []string{name})
		varBody := varBlock.Body()
		varBody.SetAttributeValue("type", cty.StringVal("string"))
		varBody.SetAttributeValue("description", cty.StringVal(fmt.Sprintf("%s variable", name)))
		varBody.SetAttributeValue("sensitive", cty.BoolVal(strings.Contains(name, "key")))
	}

	content := f.Bytes()
	if g.format {
		content = hclwrite.Format(content)
	}

	return os.WriteFile(filePath, content, 0644)
}

func (g *Generator) generateOutputs(resources []cloud.Resource) error {
	filePath := filepath.Join(g.outputDir, "outputs.tf")

	f := hclwrite.NewEmptyFile()
	rootBody := f.Body()

	for _, r := range resources {
		outputName := fmt.Sprintf("%s_%s_id", r.Cloud, sanitizeName(r.Name))
		outputBlock := rootBody.AppendNewBlock("output", []string{outputName})
		outputBody := outputBlock.Body()

		tfType := g.getTerraformType(r)
		tfName := sanitizeName(r.Name)
		outputBody.SetAttributeRaw("value", hclwrite.NewExpression(fmt.Sprintf("%s.%s.id", tfType, tfName)))
		outputBody.SetAttributeValue("description", cty.StringVal(fmt.Sprintf("ID of %s", r.Name)))
	}

	content := f.Bytes()
	if g.format {
		content = hclwrite.Format(content)
	}

	return os.WriteFile(filePath, content, 0644)
}

func (g *Generator) generateImports(resources []cloud.Resource) error {
	filePath := filepath.Join(g.outputDir, "imports.tf")

	f := hclwrite.NewEmptyFile()
	rootBody := f.Body()

	for _, r := range resources {
		tfType := g.getTerraformType(r)
		tfName := sanitizeName(r.Name)

		importBlock := rootBody.AppendNewBlock("import", nil)
		importBody := importBlock.Body()
		importBody.SetAttributeRaw("to", hclwrite.NewExpression(fmt.Sprintf("%s.%s", tfType, tfName)))
		importBody.SetAttributeValue("id", cty.StringVal(r.ID))
	}

	content := f.Bytes()
	if g.format {
		content = hclwrite.Format(content)
	}

	return os.WriteFile(filePath, content, 0644)
}

func sanitizeName(name string) string {
	if name == "" {
		return "resource"
	}
	re := regexp.MustCompile(`[^a-zA-Z0-9_]`)
	return re.ReplaceAllString(name, "_")
}