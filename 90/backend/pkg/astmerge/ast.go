package astmerge

import (
	"encoding/json"
	"fmt"
	"strings"

	"gopkg.in/yaml.v3"
)

type NodeType string

const (
	NodeTypeMapping NodeType = "mapping"
	NodeTypeSequence NodeType = "sequence"
	NodeTypeScalar   NodeType = "scalar"
	NodeTypeNull     NodeType = "null"
)

type ChangeType string

const (
	ChangeTypeAdded    ChangeType = "added"
	ChangeTypeRemoved  ChangeType = "removed"
	ChangeTypeModified ChangeType = "modified"
	ChangeTypeConflict ChangeType = "conflict"
	ChangeTypeSame     ChangeType = "same"
)

type Node struct {
	Type     NodeType            `json:"type"`
	Key      string              `json:"key,omitempty"`
	Value    interface{}         `json:"value,omitempty"`
	Children map[string]*Node    `json:"children,omitempty"`
	Items    []*Node             `json:"items,omitempty"`
	Path     string              `json:"path"`
	Change   ChangeType          `json:"change,omitempty"`
	Conflict *ConflictResolution `json:"conflict,omitempty"`
	Source   string              `json:"source,omitempty"`
}

type ConflictResolution struct {
	Base    *Node `json:"base"`
	Source  *Node `json:"source"`
	Target  *Node `json:"target"`
	Resolved *Node `json:"resolved,omitempty"`
	Selected string `json:"selected,omitempty"`
}

type MergeStrategy struct {
	ArrayMergeByKeys   map[string][]string `json:"array_merge_by_keys"`
	PreferSourcePaths  []string            `json:"prefer_source_paths"`
	PreferTargetPaths  []string            `json:"prefer_target_paths"`
	CombineArraysPaths []string            `json:"combine_arrays_paths"`
}

func DefaultMergeStrategy() *MergeStrategy {
	return &MergeStrategy{
		ArrayMergeByKeys: map[string][]string{
			".spec.template.spec.containers": {"name"},
			".spec.ports":                     {"name"},
			".spec.template.spec.volumes":     {"name"},
			".items":                          {"metadata.name"},
		},
	}
}

type MergeResult struct {
	Root          *Node  `json:"root"`
	HasConflicts  bool   `json:"has_conflicts"`
	ConflictCount int    `json:"conflict_count"`
	YAML          string `json:"yaml,omitempty"`
	JSON          string `json:"json,omitempty"`
}

type DiffResult struct {
	Changes     []*Node `json:"changes"`
	AddedCount  int     `json:"added_count"`
	RemovedCount int    `json:"removed_count"`
	ModifiedCount int   `json:"modified_count"`
}

func ParseYAML(content string) (*Node, error) {
	var root yaml.Node
	if err := yaml.Unmarshal([]byte(content), &root); err != nil {
		return nil, fmt.Errorf("parse yaml: %w", err)
	}
	if len(root.Content) == 0 {
		return &Node{Type: NodeTypeNull, Path: ""}, nil
	}
	return convertYAMLNode(root.Content[0], ""), nil
}

func ParseJSON(content string) (*Node, error) {
	var data interface{}
	if err := json.Unmarshal([]byte(content), &data); err != nil {
		return nil, fmt.Errorf("parse json: %w", err)
	}
	return convertJSONNode(data, ""), nil
}

func ParseAuto(content string) (*Node, string, error) {
	trimmed := strings.TrimSpace(content)
	if len(trimmed) > 0 {
		firstChar := trimmed[0]
		if firstChar == '{' || firstChar == '[' {
			node, err := ParseJSON(content)
			return node, "json", err
		}
	}
	node, err := ParseYAML(content)
	return node, "yaml", err
}

func convertYAMLNode(yn *yaml.Node, path string) *Node {
	node := &Node{Path: path}

	switch yn.Kind {
	case yaml.DocumentNode:
		if len(yn.Content) > 0 {
			return convertYAMLNode(yn.Content[0], path)
		}
		node.Type = NodeTypeNull

	case yaml.MappingNode:
		node.Type = NodeTypeMapping
		node.Children = make(map[string]*Node)
		for i := 0; i < len(yn.Content); i += 2 {
			key := yn.Content[i].Value
			childPath := path + "." + key
			node.Children[key] = convertYAMLNode(yn.Content[i+1], childPath)
			node.Children[key].Key = key
		}

	case yaml.SequenceNode:
		node.Type = NodeTypeSequence
		node.Items = make([]*Node, 0, len(yn.Content))
		for i, child := range yn.Content {
			childPath := fmt.Sprintf("%s[%d]", path, i)
			item := convertYAMLNode(child, childPath)
			item.Key = fmt.Sprintf("[%d]", i)
			node.Items = append(node.Items, item)
		}

	case yaml.ScalarNode:
		node.Type = NodeTypeScalar
		node.Value = yn.Value
		switch yn.Tag {
		case "!!int", "!!float":
			node.Value = yamlNumberToInterface(yn.Value)
		case "!!bool":
			node.Value = yn.Value == "true"
		case "!!null":
			node.Type = NodeTypeNull
			node.Value = nil
		}

	case yaml.AliasNode:
		return convertYAMLNode(yn.Alias, path)
	}

	return node
}

func yamlNumberToInterface(s string) interface{} {
	var i int
	if _, err := fmt.Sscanf(s, "%d", &i); err == nil {
		return i
	}
	var f float64
	if _, err := fmt.Sscanf(s, "%f", &f); err == nil {
		return f
	}
	return s
}

func convertJSONNode(data interface{}, path string) *Node {
	node := &Node{Path: path}

	switch v := data.(type) {
	case map[string]interface{}:
		node.Type = NodeTypeMapping
		node.Children = make(map[string]*Node)
		for key, val := range v {
			childPath := path + "." + key
			node.Children[key] = convertJSONNode(val, childPath)
			node.Children[key].Key = key
		}

	case []interface{}:
		node.Type = NodeTypeSequence
		node.Items = make([]*Node, 0, len(v))
		for i, item := range v {
			childPath := fmt.Sprintf("%s[%d]", path, i)
			child := convertJSONNode(item, childPath)
			child.Key = fmt.Sprintf("[%d]", i)
			node.Items = append(node.Items, child)
		}

	case nil:
		node.Type = NodeTypeNull
		node.Value = nil

	default:
		node.Type = NodeTypeScalar
		node.Value = v
	}

	return node
}

func (n *Node) ToYAML() (string, error) {
	yn := n.toYAMLNode()
	data, err := yaml.Marshal(yn)
	if err != nil {
		return "", err
	}
	return string(data), nil
}

func (n *Node) ToJSON() (string, error) {
	data := n.toInterface()
	bytes, err := json.MarshalIndent(data, "", "  ")
	if err != nil {
		return "", err
	}
	return string(bytes), nil
}

func (n *Node) toYAMLNode() *yaml.Node {
	yn := &yaml.Node{}

	switch n.Type {
	case NodeTypeMapping:
		yn.Kind = yaml.MappingNode
		yn.Tag = "!!map"
		for key, child := range n.Children {
			keyNode := &yaml.Node{Kind: yaml.ScalarNode, Value: key}
			yn.Content = append(yn.Content, keyNode, child.toYAMLNode())
		}

	case NodeTypeSequence:
		yn.Kind = yaml.SequenceNode
		yn.Tag = "!!seq"
		for _, item := range n.Items {
			yn.Content = append(yn.Content, item.toYAMLNode())
		}

	case NodeTypeScalar:
		yn.Kind = yaml.ScalarNode
		yn.Value = fmt.Sprintf("%v", n.Value)
		switch n.Value.(type) {
		case int, int64, float64:
			yn.Tag = "!!" + strings.ToLower(fmt.Sprintf("%T", n.Value))
		case bool:
			yn.Tag = "!!bool"
		}

	case NodeTypeNull:
		yn.Kind = yaml.ScalarNode
		yn.Tag = "!!null"
		yn.Value = "null"
	}

	return yn
}

func (n *Node) toInterface() interface{} {
	switch n.Type {
	case NodeTypeMapping:
		result := make(map[string]interface{})
		for key, child := range n.Children {
			result[key] = child.toInterface()
		}
		return result

	case NodeTypeSequence:
		result := make([]interface{}, 0, len(n.Items))
		for _, item := range n.Items {
			result = append(result, item.toInterface())
		}
		return result

	case NodeTypeScalar:
		return n.Value

	case NodeTypeNull:
		return nil
	}
	return nil
}

func (n *Node) Copy() *Node {
	copy := &Node{
		Type:   n.Type,
		Key:    n.Key,
		Value:  n.Value,
		Path:   n.Path,
		Change: n.Change,
		Source: n.Source,
	}

	if n.Children != nil {
		copy.Children = make(map[string]*Node)
		for k, v := range n.Children {
			copy.Children[k] = v.Copy()
		}
	}

	if n.Items != nil {
		copy.Items = make([]*Node, 0, len(n.Items))
		for _, item := range n.Items {
			copy.Items = append(copy.Items, item.Copy())
		}
	}

	if n.Conflict != nil {
		copy.Conflict = &ConflictResolution{
			Selected: n.Conflict.Selected,
		}
		if n.Conflict.Base != nil {
			copy.Conflict.Base = n.Conflict.Base.Copy()
		}
		if n.Conflict.Source != nil {
			copy.Conflict.Source = n.Conflict.Source.Copy()
		}
		if n.Conflict.Target != nil {
			copy.Conflict.Target = n.Conflict.Target.Copy()
		}
		if n.Conflict.Resolved != nil {
			copy.Conflict.Resolved = n.Conflict.Resolved.Copy()
		}
	}

	return copy
}

func (n *Node) Equals(other *Node) bool {
	if n.Type != other.Type {
		return false
	}
	if n.Value != other.Value {
		return false
	}
	if len(n.Children) != len(other.Children) {
		return false
	}
	for k, v := range n.Children {
		ov, ok := other.Children[k]
		if !ok || !v.Equals(ov) {
			return false
		}
	}
	if len(n.Items) != len(other.Items) {
		return false
	}
	for i, item := range n.Items {
		if !item.Equals(other.Items[i]) {
			return false
		}
	}
	return true
}

func (n *Node) GetAllConflicts() []*Node {
	var conflicts []*Node

	if n.Conflict != nil && n.Conflict.Selected == "" {
		conflicts = append(conflicts, n)
	}

	for _, child := range n.Children {
		conflicts = append(conflicts, child.GetAllConflicts()...)
	}

	for _, item := range n.Items {
		conflicts = append(conflicts, item.GetAllConflicts()...)
	}

	return conflicts
}

func (n *Node) ResolveConflict(path string, selection string) bool {
	if n.Path == path && n.Conflict != nil {
		n.Conflict.Selected = selection
		switch selection {
		case "source":
			*n = *n.Conflict.Source.Copy()
		case "target":
			*n = *n.Conflict.Target.Copy()
		case "base":
			*n = *n.Conflict.Base.Copy()
		}
		return true
	}

	for _, child := range n.Children {
		if child.ResolveConflict(path, selection) {
			return true
		}
	}

	for _, item := range n.Items {
		if item.ResolveConflict(path, selection) {
			return true
		}
	}

	return false
}
