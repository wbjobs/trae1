package drift

import (
	"bytes"
	"crypto/sha256"
	"fmt"
	"os"
	"path/filepath"

	"github.com/hashicorp/hcl/v2/hclwrite"
	"github.com/zclconf/go-cty/cty"

	"terraform-config-generator/cloud"
)

type Detector struct {
	outputDir string
	clients   *cloud.CloudClients
}

type DriftResult struct {
	ID          string
	Type        string
	Cloud       cloud.CloudType
	ResourceID  string
	Description string
	Actual      map[string]interface{}
	Expected    map[string]interface{}
}

func NewDetector(outputDir string, clients *cloud.CloudClients) *Detector {
	return &Detector{
		outputDir: outputDir,
		clients:   clients,
	}
}

func (d *Detector) Detect() ([]DriftResult, error) {
	var drifts []DriftResult

	actualResources, err := cloud.FetchAllResources(d.clients)
	if err != nil {
		return nil, fmt.Errorf("获取实际资源失败: %w", err)
	}

	expectedResources, err := d.loadExpectedResources()
	if err != nil {
		return nil, fmt.Errorf("加载预期资源失败: %w", err)
	}

	actualMap := make(map[string]cloud.Resource)
	for _, r := range actualResources {
		key := fmt.Sprintf("%s_%s", r.Cloud, r.ID)
		actualMap[key] = r
	}

	for key, expected := range expectedResources {
		if actual, exists := actualMap[key]; exists {
			if diff := compareResources(actual, expected); diff != nil {
				drifts = append(drifts, *diff)
			}
			delete(actualMap, key)
		} else {
			drifts = append(drifts, DriftResult{
				ID:          key,
				Type:        expected.Type,
				Cloud:       expected.Cloud,
				ResourceID:  expected.ID,
				Description: "资源已被删除",
				Actual:      nil,
				Expected:    expected.Attributes,
			})
		}
	}

	for key, actual := range actualMap {
		drifts = append(drifts, DriftResult{
			ID:          key,
			Type:        actual.Type,
			Cloud:       actual.Cloud,
			ResourceID:  actual.ID,
			Description: "发现未管理的资源",
			Actual:      actual.Attributes,
			Expected:    nil,
		})
	}

	return drifts, nil
}

func compareResources(actual, expected cloud.Resource) *DriftResult {
	if actual.Name != expected.Name {
		return &DriftResult{
			ID:          fmt.Sprintf("%s_%s", actual.Cloud, actual.ID),
			Type:        actual.Type,
			Cloud:       actual.Cloud,
			ResourceID:  actual.ID,
			Description: fmt.Sprintf("名称变更: %s -> %s", expected.Name, actual.Name),
			Actual:      map[string]interface{}{"name": actual.Name},
			Expected:    map[string]interface{}{"name": expected.Name},
		}
	}

	for key, expectedVal := range expected.Attributes {
		if actualVal, exists := actual.Attributes[key]; exists {
			if fmt.Sprintf("%v", actualVal) != fmt.Sprintf("%v", expectedVal) {
				return &DriftResult{
					ID:          fmt.Sprintf("%s_%s", actual.Cloud, actual.ID),
					Type:        actual.Type,
					Cloud:       actual.Cloud,
					ResourceID:  actual.ID,
					Description: fmt.Sprintf("属性 %s 变更", key),
					Actual:      map[string]interface{}{key: actualVal},
					Expected:    map[string]interface{}{key: expectedVal},
				}
			}
		} else {
			return &DriftResult{
				ID:          fmt.Sprintf("%s_%s", actual.Cloud, actual.ID),
				Type:        actual.Type,
				Cloud:       actual.Cloud,
				ResourceID:  actual.ID,
				Description: fmt.Sprintf("属性 %s 缺失", key),
				Actual:      nil,
				Expected:    map[string]interface{}{key: expectedVal},
			}
		}
	}

	return nil
}

func (d *Detector) loadExpectedResources() (map[string]cloud.Resource, error) {
	result := make(map[string]cloud.Resource)

	files, err := filepath.Glob(filepath.Join(d.outputDir, "*.tf"))
	if err != nil {
		return nil, err
	}

	for _, file := range files {
		content, err := os.ReadFile(file)
		if err != nil {
			return nil, err
		}

		f, diags := hclwrite.ParseConfig(content, file, hclwrite.Pos{})
		if diags.HasErrors() {
			return nil, fmt.Errorf("解析HCL失败: %w", diags)
		}

		for _, block := range f.Body().Blocks() {
			if block.Type() == "resource" {
				labels := block.Labels()
				if len(labels) >= 2 {
					cloudType, resourceType := parseResourceType(labels[0])
					name := labels[1]

					attrs := make(map[string]interface{})
					for _, attr := range block.Body().Attributes() {
						attrs[attr] = block.Body().GetAttributeValue(attr).AsString()
					}

					key := fmt.Sprintf("%s_%s", cloudType, name)
					result[key] = cloud.Resource{
						ID:         name,
						Name:       name,
						Type:       resourceType,
						Cloud:      cloud.CloudType(cloudType),
						Attributes: attrs,
					}
				}
			}
		}
	}

	return result, nil
}

func parseResourceType(tfType string) (string, string) {
	switch {
	case len(tfType) > 4 && tfType[:4] == "aws_":
		return "aws", tfType[4:]
	case len(tfType) > 9 && tfType[:9] == "alicloud_":
		return "aliyun", tfType[9:]
	case len(tfType) > 14 && tfType[:14] == "tencentcloud_":
		return "tencent", tfType[14:]
	default:
		return "unknown", tfType
	}
}

func (d *Detector) GeneratePatch(drifts []DriftResult) error {
	filePath := filepath.Join(d.outputDir, "drift_patch.tf")

	f := hclwrite.NewEmptyFile()
	rootBody := f.Body()

	rootBody.AppendNewBlock("comment", []string{"AUTOMATICALLY_GENERATED_DRIFT_PATCH"})
	rootBody.AppendNewBlock("comment", []string{"This file contains detected drift changes"})

	for _, drift := range drifts {
		if drift.Actual == nil {
			rootBody.AppendNewBlock("comment", []string{fmt.Sprintf("DELETED: %s %s", drift.Type, drift.ResourceID)})
			continue
		}

		if drift.Expected == nil {
			rootBody.AppendNewBlock("comment", []string{fmt.Sprintf("NEW: %s %s", drift.Type, drift.ResourceID)})
			continue
		}

		resourceBlock := rootBody.AppendNewBlock("resource", []string{getTFType(drift), fmt.Sprintf("%s_patch", drift.ResourceID)})
		resourceBody := resourceBlock.Body()

		for k, v := range drift.Actual {
			resourceBody.SetAttributeValue(k, cty.StringVal(fmt.Sprintf("%v", v)))
		}
	}

	content := f.Bytes()
	return os.WriteFile(filePath, content, 0644)
}

func getTFType(drift DriftResult) string {
	switch drift.Cloud {
	case cloud.AWS:
		return fmt.Sprintf("aws_%s", drift.Type)
	case cloud.Aliyun:
		return fmt.Sprintf("alicloud_%s", drift.Type)
	case cloud.Tencent:
		return fmt.Sprintf("tencentcloud_%s", drift.Type)
	default:
		return fmt.Sprintf("%s_%s", drift.Cloud, drift.Type)
	}
}

func (d *Detector) GenerateChecksum() (string, error) {
	var buffer bytes.Buffer

	resources, err := cloud.FetchAllResources(d.clients)
	if err != nil {
		return "", err
	}

	for _, r := range resources {
		buffer.WriteString(fmt.Sprintf("%s:%s:%s\n", r.Cloud, r.Type, r.ID))
		for k, v := range r.Attributes {
			buffer.WriteString(fmt.Sprintf("\t%s=%v\n", k, v))
		}
	}

	hash := sha256.Sum256(buffer.Bytes())
	return fmt.Sprintf("%x", hash), nil
}