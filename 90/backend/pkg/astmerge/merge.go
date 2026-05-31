package astmerge

import (
	"fmt"
	"strings"
)

func ThreeWayMerge(baseStr, sourceStr, targetStr string, strategy *MergeStrategy) (*MergeResult, error) {
	if strategy == nil {
		strategy = DefaultMergeStrategy()
	}

	base, baseFormat, err := ParseAuto(baseStr)
	if err != nil {
		return nil, fmt.Errorf("parse base: %w", err)
	}

	source, sourceFormat, err := ParseAuto(sourceStr)
	if err != nil {
		return nil, fmt.Errorf("parse source: %w", err)
	}

	target, targetFormat, err := ParseAuto(targetStr)
	if err != nil {
		return nil, fmt.Errorf("parse target: %w", err)
	}

	_ = sourceFormat
	_ = targetFormat

	result := &MergeResult{}

	merged, hasConflicts := mergeNodes(base, source, target, "", strategy, result)
	result.Root = merged
	result.HasConflicts = hasConflicts

	conflicts := merged.GetAllConflicts()
	result.ConflictCount = len(conflicts)

	if !hasConflicts {
		if baseFormat == "json" {
			jsonStr, err := merged.ToJSON()
			if err == nil {
				result.JSON = jsonStr
			}
		}
		yamlStr, err := merged.ToYAML()
		if err == nil {
			result.YAML = yamlStr
		}
	}

	return result, nil
}

func mergeNodes(base, source, target *Node, path string, strategy *MergeStrategy, result *MergeResult) (*Node, bool) {
	hasConflicts := false

	if base == nil {
		base = &Node{Type: NodeTypeNull, Path: path}
	}

	sourceChanged := !source.Equals(base)
	targetChanged := !target.Equals(base)

	if !sourceChanged && !targetChanged {
		merged := base.Copy()
		merged.Change = ChangeTypeSame
		return merged, false
	}

	if sourceChanged && !targetChanged {
		merged := source.Copy()
		merged.Change = ChangeTypeModified
		merged.Source = "source"
		return merged, false
	}

	if !sourceChanged && targetChanged {
		merged := target.Copy()
		merged.Change = ChangeTypeModified
		merged.Source = "target"
		return merged, false
	}

	if source.Equals(target) {
		merged := source.Copy()
		merged.Change = ChangeTypeModified
		merged.Source = "both"
		return merged, false
	}

	if source.Type == NodeTypeMapping && target.Type == NodeTypeMapping && base.Type == NodeTypeMapping {
		return mergeMappings(base, source, target, path, strategy, result)
	}

	if source.Type == NodeTypeSequence && target.Type == NodeTypeSequence {
		return mergeSequences(base, source, target, path, strategy, result)
	}

	if source.Type == NodeTypeScalar && target.Type == NodeTypeScalar {
		if strategy.pathMatches(source.Path, strategy.PreferSourcePaths) {
			merged := source.Copy()
			merged.Change = ChangeTypeModified
			merged.Source = "source"
			return merged, false
		}
		if strategy.pathMatches(source.Path, strategy.PreferTargetPaths) {
			merged := target.Copy()
			merged.Change = ChangeTypeModified
			merged.Source = "target"
			return merged, false
		}
	}

	conflictNode := &Node{
		Type:   source.Type,
		Key:    source.Key,
		Path:   path,
		Change: ChangeTypeConflict,
		Conflict: &ConflictResolution{
			Base:   base.Copy(),
			Source: source.Copy(),
			Target: target.Copy(),
		},
	}

	return conflictNode, true
}

func mergeMappings(base, source, target *Node, path string, strategy *MergeStrategy, result *MergeResult) (*Node, bool) {
	merged := &Node{
		Type:     NodeTypeMapping,
		Children: make(map[string]*Node),
		Path:     path,
		Key:      source.Key,
	}
	hasConflicts := false

	allKeys := make(map[string]bool)
	for k := range base.Children {
		allKeys[k] = true
	}
	for k := range source.Children {
		allKeys[k] = true
	}
	for k := range target.Children {
		allKeys[k] = true
	}

	for key := range allKeys {
		childPath := path + "." + key

		var baseChild, sourceChild, targetChild *Node

		if base != nil && base.Children != nil {
			baseChild = base.Children[key]
		}
		if source != nil && source.Children != nil {
			sourceChild = source.Children[key]
		}
		if target != nil && target.Children != nil {
			targetChild = target.Children[key]
		}

		if sourceChild == nil && targetChild == nil {
			continue
		}

		if sourceChild == nil && targetChild != nil {
			nt := targetChild.Copy()
			nt.Change = ChangeTypeAdded
			nt.Source = "target"
			merged.Children[key] = nt
			continue
		}

		if sourceChild != nil && targetChild == nil {
			ns := sourceChild.Copy()
			ns.Change = ChangeTypeAdded
			ns.Source = "source"
			merged.Children[key] = ns
			continue
		}

		if baseChild == nil {
			baseChild = &Node{Type: NodeTypeNull, Path: childPath}
		}

		childMerged, childConflict := mergeNodes(baseChild, sourceChild, targetChild, childPath, strategy, result)
		merged.Children[key] = childMerged
		if childConflict {
			hasConflicts = true
		}
	}

	if hasConflicts {
		merged.Change = ChangeTypeConflict
	} else {
		merged.Change = ChangeTypeModified
	}

	return merged, hasConflicts
}

func mergeSequences(base, source, target *Node, path string, strategy *MergeStrategy, result *MergeResult) (*Node, bool) {
	hasConflicts := false

	mergeByKeys := strategy.getArrayMergeKeys(path)

	if strategy.pathMatches(path, strategy.CombineArraysPaths) {
		return combineArrays(base, source, target, path)
	}

	if len(mergeByKeys) > 0 {
		return mergeArraysByKeys(base, source, target, path, mergeByKeys, strategy, result)
	}

	return mergeArraysByIndex(base, source, target, path, strategy, result)
}

func (s *MergeStrategy) getArrayMergeKeys(path string) []string {
	for pattern, keys := range s.ArrayMergeByKeys {
		if strings.HasSuffix(path, pattern) || path == pattern {
			return keys
		}
	}
	return nil
}

func (s *MergeStrategy) pathMatches(path string, patterns []string) bool {
	for _, pattern := range patterns {
		if strings.HasSuffix(path, pattern) || path == pattern || strings.Contains(path, pattern) {
			return true
		}
	}
	return false
}

func combineArrays(base, source, target *Node, path string) (*Node, bool) {
	seen := make(map[string]int)
	items := make([]*Node, 0)

	addItem := func(n *Node, source string) {
		key := getNodeSignature(n)
		if idx, ok := seen[key]; ok {
			items[idx] = n.Copy()
			items[idx].Source = source
			return
		}
		nc := n.Copy()
		nc.Change = ChangeTypeAdded
		nc.Source = source
		items = append(items, nc)
		seen[key] = len(items) - 1
	}

	for _, item := range source.Items {
		addItem(item, "source")
	}

	for _, item := range target.Items {
		addItem(item, "target")
	}

	return &Node{
		Type:   NodeTypeSequence,
		Items:  items,
		Path:   path,
		Key:    source.Key,
		Change: ChangeTypeModified,
	}, false
}

func getNodeSignature(n *Node) string {
	if n.Type == NodeTypeMapping && n.Children != nil {
		if name, ok := n.Children["name"]; ok && name.Type == NodeTypeScalar {
			return fmt.Sprintf("name=%v", name.Value)
		}
	}
	return fmt.Sprintf("%v", n.Value)
}

func getMergeKeyValue(n *Node, keys []string) string {
	if n.Type != NodeTypeMapping {
		return fmt.Sprintf("%v", n.Value)
	}

	var parts []string
	for _, key := range keys {
		parts = append(parts, getNestedValue(n, key))
	}
	return strings.Join(parts, "|")
}

func getNestedValue(n *Node, path string) string {
	parts := strings.Split(path, ".")
	current := n

	for _, part := range parts {
		if current == nil || current.Type != NodeTypeMapping {
			return ""
		}
		var ok bool
		current, ok = current.Children[part]
		if !ok {
			return ""
		}
	}

	if current != nil && current.Type == NodeTypeScalar {
		return fmt.Sprintf("%v", current.Value)
	}
	return ""
}

func mergeArraysByKeys(base, source, target *Node, path string, mergeKeys []string, strategy *MergeStrategy, result *MergeResult) (*Node, bool) {
	hasConflicts := false

	baseMap := make(map[string]*Node)
	if base != nil && base.Type == NodeTypeSequence {
		for _, item := range base.Items {
			key := getMergeKeyValue(item, mergeKeys)
			baseMap[key] = item
		}
	}

	sourceMap := make(map[string]*Node)
	for _, item := range source.Items {
		key := getMergeKeyValue(item, mergeKeys)
		sourceMap[key] = item
	}

	targetMap := make(map[string]*Node)
	for _, item := range target.Items {
		key := getMergeKeyValue(item, mergeKeys)
		targetMap[key] = item
	}

	allKeys := make(map[string]bool)
	for k := range sourceMap {
		allKeys[k] = true
	}
	for k := range targetMap {
		allKeys[k] = true
	}

	mergedItems := make([]*Node, 0)

	for key := range allKeys {
		baseItem := baseMap[key]
		sourceItem := sourceMap[key]
		targetItem := targetMap[key]
		itemPath := fmt.Sprintf("%s[%s]", path, key)

		if sourceItem != nil && targetItem == nil {
			ns := sourceItem.Copy()
			ns.Change = ChangeTypeAdded
			ns.Source = "source"
			ns.Path = itemPath
			mergedItems = append(mergedItems, ns)
			continue
		}

		if sourceItem == nil && targetItem != nil {
			nt := targetItem.Copy()
			nt.Change = ChangeTypeAdded
			nt.Source = "target"
			nt.Path = itemPath
			mergedItems = append(mergedItems, nt)
			continue
		}

		if baseItem == nil {
			baseItem = &Node{Type: NodeTypeNull, Path: itemPath}
		}

		merged, conflict := mergeNodes(baseItem, sourceItem, targetItem, itemPath, strategy, result)
		mergedItems = append(mergedItems, merged)
		if conflict {
			hasConflicts = true
		}
	}

	return &Node{
		Type:   NodeTypeSequence,
		Items:  mergedItems,
		Path:   path,
		Key:    source.Key,
		Change: ChangeTypeModified,
	}, hasConflicts
}

func mergeArraysByIndex(base, source, target *Node, path string, strategy *MergeStrategy, result *MergeResult) (*Node, bool) {
	hasConflicts := false

	maxLen := len(source.Items)
	if len(target.Items) > maxLen {
		maxLen = len(target.Items)
	}

	mergedItems := make([]*Node, 0, maxLen)

	for i := 0; i < maxLen; i++ {
		itemPath := fmt.Sprintf("%s[%d]", path, i)

		var baseItem, sourceItem, targetItem *Node

		if base != nil && base.Type == NodeTypeSequence && i < len(base.Items) {
			baseItem = base.Items[i]
		}
		if i < len(source.Items) {
			sourceItem = source.Items[i]
		}
		if i < len(target.Items) {
			targetItem = target.Items[i]
		}

		if sourceItem == nil && targetItem != nil {
			nt := targetItem.Copy()
			nt.Change = ChangeTypeAdded
			nt.Source = "target"
			nt.Path = itemPath
			mergedItems = append(mergedItems, nt)
			continue
		}

		if sourceItem != nil && targetItem == nil {
			ns := sourceItem.Copy()
			ns.Change = ChangeTypeAdded
			ns.Source = "source"
			ns.Path = itemPath
			mergedItems = append(mergedItems, ns)
			continue
		}

		if baseItem == nil {
			baseItem = &Node{Type: NodeTypeNull, Path: itemPath}
		}

		merged, conflict := mergeNodes(baseItem, sourceItem, targetItem, itemPath, strategy, result)
		mergedItems = append(mergedItems, merged)
		if conflict {
			hasConflicts = true
		}
	}

	return &Node{
		Type:   NodeTypeSequence,
		Items:  mergedItems,
		Path:   path,
		Key:    source.Key,
		Change: ChangeTypeModified,
	}, hasConflicts
}

func Diff(base, modified *Node) *DiffResult {
	result := &DiffResult{
		Changes: make([]*Node, 0),
	}

	diffNodes(base, modified, result)

	return result
}

func diffNodes(base, modified *Node, result *DiffResult) {
	if base == nil && modified == nil {
		return
	}

	if base == nil && modified != nil {
		nc := modified.Copy()
		nc.Change = ChangeTypeAdded
		result.Changes = append(result.Changes, nc)
		result.AddedCount++
		return
	}

	if base != nil && modified == nil {
		nc := base.Copy()
		nc.Change = ChangeTypeRemoved
		result.Changes = append(result.Changes, nc)
		result.RemovedCount++
		return
	}

	if base.Equals(modified) {
		return
	}

	if base.Type == NodeTypeMapping && modified.Type == NodeTypeMapping {
		allKeys := make(map[string]bool)
		for k := range base.Children {
			allKeys[k] = true
		}
		for k := range modified.Children {
			allKeys[k] = true
		}

		for k := range allKeys {
			diffNodes(base.Children[k], modified.Children[k], result)
		}
		return
	}

	if base.Type == NodeTypeSequence && modified.Type == NodeTypeSequence {
		minLen := len(base.Items)
		if len(modified.Items) < minLen {
			minLen = len(modified.Items)
		}

		for i := 0; i < minLen; i++ {
			diffNodes(base.Items[i], modified.Items[i], result)
		}

		for i := minLen; i < len(base.Items); i++ {
			nc := base.Items[i].Copy()
			nc.Change = ChangeTypeRemoved
			result.Changes = append(result.Changes, nc)
			result.RemovedCount++
		}

		for i := minLen; i < len(modified.Items); i++ {
			nc := modified.Items[i].Copy()
			nc.Change = ChangeTypeAdded
			result.Changes = append(result.Changes, nc)
			result.AddedCount++
		}
		return
	}

	nc := modified.Copy()
	nc.Change = ChangeTypeModified
	result.Changes = append(result.Changes, nc)
	result.ModifiedCount++
}

func ResolveConflicts(root *Node, resolutions map[string]string) (bool, error) {
	for path, selection := range resolutions {
		if !root.ResolveConflict(path, selection) {
			return false, fmt.Errorf("conflict not found at path: %s", path)
		}
	}

	remaining := root.GetAllConflicts()
	return len(remaining) == 0, nil
}
