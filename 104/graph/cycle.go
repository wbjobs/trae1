package graph

import (
	"fmt"
	"os"
	"path/filepath"
	"sort"
	"strings"
)

type CycleResult struct {
	Cycles     [][]string
	IsDAG      bool
	Suggestions []string
}

func (g *DependencyGraph) DetectCycles() CycleResult {
	result := CycleResult{
		Cycles:     [][]string{},
		IsDAG:      true,
		Suggestions: []string{},
	}

	cycles := g.tarjan()
	if len(cycles) > 0 {
		result.IsDAG = false
		result.Cycles = cycles
		result.Suggestions = g.generateSuggestions(cycles)
	}

	return result
}

func (g *DependencyGraph) tarjan() [][]string {
	var cycles [][]string
	index := make(map[string]int)
	low := make(map[string]int)
	onStack := make(map[string]bool)
	stack := []string{}
	time := 0

	var strongConnect func(v string)
	strongConnect = func(v string) {
		index[v] = time
		low[v] = time
		time++
		stack = append(stack, v)
		onStack[v] = true

		if neighbors, exists := g.Edges[v]; exists {
			for _, w := range neighbors {
				if _, exists := index[w]; !exists {
					strongConnect(w)
					if low[w] < low[v] {
						low[v] = low[w]
					}
				} else if onStack[w] {
					if index[w] < low[v] {
						low[v] = index[w]
					}
				}
			}
		}

		if low[v] == index[v] {
			var cycle []string
			for {
				w := stack[len(stack)-1]
				stack = stack[:len(stack)-1]
				onStack[w] = false
				cycle = append(cycle, w)
				if w == v {
					break
				}
			}
			if len(cycle) > 1 {
				cycles = append(cycles, cycle)
			}
		}
	}

	for id := range g.Nodes {
		if _, exists := index[id]; !exists {
			strongConnect(id)
		}
	}

	return cycles
}

func (g *DependencyGraph) generateSuggestions(cycles [][]string) []string {
	var suggestions []string
	for _, cycle := range cycles {
		nodeNames := make([]string, len(cycle))
		for i, id := range cycle {
			if node, exists := g.Nodes[id]; exists {
				nodeNames[i] = fmt.Sprintf("%s (%s)", node.Name, node.Type)
			} else {
				nodeNames[i] = id
			}
		}

		suggestions = append(suggestions, fmt.Sprintf(
			"检测到循环依赖: %s",
			strings.Join(nodeNames, " -> "),
		))
		suggestions = append(suggestions, fmt.Sprintf(
			"  建议: 考虑使用depends_on显式声明，或重新设计架构避免循环引用",
		))
	}
	return suggestions
}

func (g *DependencyGraph) ExportCycleDOT(outputDir string, cycles [][]string) error {
	filePath := filepath.Join(outputDir, "cycle_detection.dot")

	f, err := os.Create(filePath)
	if err != nil {
		return fmt.Errorf("创建循环检测DOT文件失败: %w", err)
	}
	defer f.Close()

	fmt.Fprintln(f, "digraph cycle_detection {")
	fmt.Fprintln(f, "\trankdir=TB;")
	fmt.Fprintln(f, "\tnode [shape=box, style=\"filled,rounded\", fontname=\"Arial\"];")

	cycleNodeSet := make(map[string]bool)
	for _, cycle := range cycles {
		for _, id := range cycle {
			cycleNodeSet[id] = true
		}
	}

	nodeColors := map[string]string{
		"vpc":            "#4CAF50",
		"subnet":         "#2196F3",
		"security_group": "#FF9800",
		"instance":       "#F44336",
		"slb":            "#9C27B0",
	}

	ids := make([]string, 0, len(g.Nodes))
	for id := range g.Nodes {
		ids = append(ids, id)
	}
	sort.Strings(ids)

	for _, id := range ids {
		node := g.Nodes[id]
		color, exists := nodeColors[node.Type]
		if !exists {
			color = "#9E9E9E"
		}

		if cycleNodeSet[id] {
			fmt.Fprintf(f, "\t\"%s\" [label=\"%s (%s)\", fillcolor=\"%s\", style=\"filled,rounded,dashed\", penwidth=2];\n",
				id, node.Name, node.Type, "#FFEB3B")
		} else {
			fmt.Fprintf(f, "\t\"%s\" [label=\"%s (%s)\", fillcolor=\"%s\"];\n", id, node.Name, node.Type, color)
		}
	}

	for from, toList := range g.Edges {
		for _, to := range toList {
			if cycleNodeSet[from] && cycleNodeSet[to] {
				fmt.Fprintf(f, "\t\"%s\" -> \"%s\" [color=\"#F44336\", penwidth=2, label=\"循环依赖\"];\n", from, to)
			} else {
				fmt.Fprintf(f, "\t\"%s\" -> \"%s\";\n", from, to)
			}
		}
	}

	fmt.Fprintln(f, "\n\tlegend [shape=none, margin=0, label=<")
	fmt.Fprintln(f, "\t\t<table border=\"0\" cellborder=\"1\" cellspacing=\"0\">")
	fmt.Fprintln(f, "\t\t\t<tr><td><b>图例</b></td></tr>")
	fmt.Fprintln(f, "\t\t\t<tr><td bgcolor=\"#FFEB3B\">循环依赖节点</td></tr>")
	fmt.Fprintln(f, "\t\t\t<tr><td><font color=\"#F44336\">循环依赖边</font></td></tr>")
	fmt.Fprintln(f, "\t\t</table>>];")
	fmt.Fprintln(f, "}")

	return nil
}

func (g *DependencyGraph) BreakCycles(cycles [][]string) []DependencyAdjustment {
	var adjustments []DependencyAdjustment

	for _, cycle := range cycles {
		if len(cycle) >= 2 {
			adjustment := DependencyAdjustment{
				From:           cycle[len(cycle)-1],
				To:             cycle[0],
				Action:         "remove",
				Reason:         fmt.Sprintf("打破循环: %s -> %s", cycle[len(cycle)-1], cycle[0]),
				Suggestion:     "考虑使用depends_on显式声明替代隐式依赖",
				OriginalCycle:  cycle,
			}
			adjustments = append(adjustments, adjustment)
		}
	}

	return adjustments
}

type DependencyAdjustment struct {
	From          string
	To            string
	Action        string
	Reason        string
	Suggestion    string
	OriginalCycle []string
}

func (g *DependencyGraph) ApplyAdjustments(adjustments []DependencyAdjustment) {
	for _, adj := range adjustments {
		if adj.Action == "remove" {
			if edges, exists := g.Edges[adj.From]; exists {
				newEdges := []string{}
				for _, e := range edges {
					if e != adj.To {
						newEdges = append(newEdges, e)
					}
				}
				if len(newEdges) == 0 {
					delete(g.Edges, adj.From)
				} else {
					g.Edges[adj.From] = newEdges
				}
			}
		} else if adj.Action == "add" {
			g.Edges[adj.From] = append(g.Edges[adj.From], adj.To)
		}
	}
}

func (g *DependencyGraph) TopologicalSort() ([]string, error) {
	inDegree := make(map[string]int)
	for id := range g.Nodes {
		inDegree[id] = 0
	}

	for _, toList := range g.Edges {
		for _, to := range toList {
			inDegree[to]++
		}
	}

	queue := []string{}
	for id, degree := range inDegree {
		if degree == 0 {
			queue = append(queue, id)
		}
	}

	var result []string
	for len(queue) > 0 {
		node := queue[0]
		queue = queue[1:]
		result = append(result, node)

		if neighbors, exists := g.Edges[node]; exists {
			for _, neighbor := range neighbors {
				inDegree[neighbor]--
				if inDegree[neighbor] == 0 {
					queue = append(queue, neighbor)
				}
			}
		}
	}

	if len(result) != len(g.Nodes) {
		return nil, fmt.Errorf("图中存在循环依赖，无法进行拓扑排序")
	}

	return result, nil
}