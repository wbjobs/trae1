package graph

import (
	"fmt"
	"os"
	"path/filepath"
	"sort"

	"terraform-config-generator/cloud"
)

type DependencyGraph struct {
	Nodes map[string]*Node
	Edges map[string][]string
}

type Node struct {
	ID       string
	Name     string
	Type     string
	Cloud    cloud.CloudType
	Resource cloud.Resource
}

type GraphBuilder struct{}

func NewGraphBuilder() *GraphBuilder {
	return &GraphBuilder{}
}

func (b *GraphBuilder) BuildDependencyGraph(resources []cloud.Resource) *DependencyGraph {
	graph := &DependencyGraph{
		Nodes: make(map[string]*Node),
		Edges: make(map[string][]string),
	}

	resourceMap := make(map[string]cloud.Resource)
	for _, r := range resources {
		resourceMap[r.ID] = r
	}

	for _, r := range resources {
		node := &Node{
			ID:       r.ID,
			Name:     r.Name,
			Type:     r.Type,
			Cloud:    r.Cloud,
			Resource: r,
		}
		graph.Nodes[r.ID] = node

		if r.ParentID != "" {
			if _, exists := resourceMap[r.ParentID]; exists {
				graph.Edges[r.ParentID] = append(graph.Edges[r.ParentID], r.ID)
			}
		}

		for _, depID := range r.DependsOn {
			if _, exists := resourceMap[depID]; exists && depID != r.ParentID {
				graph.Edges[depID] = append(graph.Edges[depID], r.ID)
			}
		}
	}

	return graph
}

func (g *DependencyGraph) ExportDOT(outputDir string) error {
	filePath := filepath.Join(outputDir, "dependencies.dot")

	f, err := os.Create(filePath)
	if err != nil {
		return fmt.Errorf("创建DOT文件失败: %w", err)
	}
	defer f.Close()

	fmt.Fprintln(f, "digraph terraform_resources {")
	fmt.Fprintln(f, "\trankdir=TB;")
	fmt.Fprintln(f, "\tnode [shape=box, style=\"filled,rounded\", fontname=\"Arial\"];")

	nodeColors := map[string]string{
		"vpc":            "#4CAF50",
		"subnet":         "#2196F3",
		"security_group": "#FF9800",
		"instance":       "#F44336",
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
		fmt.Fprintf(f, "\t\"%s\" [label=\"%s (%s)\", fillcolor=\"%s\"];\n", id, node.Name, node.Type, color)
	}

	for from, toList := range g.Edges {
		for _, to := range toList {
			fmt.Fprintf(f, "\t\"%s\" -> \"%s\";\n", from, to)
		}
	}

	fmt.Fprintln(f, "}")
	return nil
}

func (g *DependencyGraph) ExportMermaid(outputDir string) error {
	filePath := filepath.Join(outputDir, "dependencies.mmd")

	f, err := os.Create(filePath)
	if err != nil {
		return fmt.Errorf("创建Mermaid文件失败: %w", err)
	}
	defer f.Close()

	fmt.Fprintln(f, "graph TD")

	nodeColors := map[string]string{
		"vpc":            "green",
		"subnet":         "blue",
		"security_group": "orange",
		"instance":       "red",
	}

	ids := make([]string, 0, len(g.Nodes))
	for id := range g.Nodes {
		ids = append(ids, id)
	}
	sort.Strings(ids)

	nodeIDs := make(map[string]string)
	counter := 0

	for _, id := range ids {
		node := g.Nodes[id]
		nodeID := fmt.Sprintf("node%d", counter)
		nodeIDs[id] = nodeID
		counter++

		color, exists := nodeColors[node.Type]
		if !exists {
			color = "gray"
		}
		fmt.Fprintf(f, "\t%s[\"%s (%s)\"]:::%s\n", nodeID, node.Name, node.Type, color)
	}

	for from, toList := range g.Edges {
		for _, to := range toList {
			fmt.Fprintf(f, "\t%s --> %s\n", nodeIDs[from], nodeIDs[to])
		}
	}

	fmt.Fprintln(f, "\nclassDef green fill:#4CAF50,color:#fff;")
	fmt.Fprintln(f, "classDef blue fill:#2196F3,color:#fff;")
	fmt.Fprintln(f, "classDef orange fill:#FF9800,color:#fff;")
	fmt.Fprintln(f, "classDef red fill:#F44336,color:#fff;")
	fmt.Fprintln(f, "classDef gray fill:#9E9E9E,color:#fff;")

	return nil
}