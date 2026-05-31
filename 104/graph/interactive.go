package graph

import (
	"bufio"
	"fmt"
	"os"
	"strings"

	"terraform-config-generator/cloud"
)

type InteractiveEditor struct {
	Graph      *DependencyGraph
	Resources  []cloud.Resource
	Adjustments []DependencyAdjustment
}

func NewInteractiveEditor(graph *DependencyGraph, resources []cloud.Resource) *InteractiveEditor {
	return &InteractiveEditor{
		Graph:      graph,
		Resources:  resources,
		Adjustments: []DependencyAdjustment{},
	}
}

func (e *InteractiveEditor) Run() error {
	scanner := bufio.NewScanner(os.Stdin)

	fmt.Println("\n=== 交互式依赖关系编辑器 ===")
	fmt.Println("输入 'help' 查看可用命令")
	fmt.Println("输入 'exit' 退出并保存更改")
	fmt.Println()

	for {
		fmt.Print("> ")
		scanner.Scan()
		input := strings.TrimSpace(scanner.Text())

		if input == "" {
			continue
		}

		parts := strings.Fields(input)
		if len(parts) == 0 {
			continue
		}

		command := strings.ToLower(parts[0])
		switch command {
		case "help":
			e.showHelp()
		case "exit":
			return e.exit()
		case "quit":
			return nil
		case "show":
			e.showGraph()
		case "cycles":
			e.showCycles()
		case "list":
			e.listResources()
		case "add":
			if len(parts) >= 3 {
				e.addDependency(parts[1], parts[2])
			} else {
				fmt.Println("用法: add <from> <to>")
			}
		case "remove":
			if len(parts) >= 3 {
				e.removeDependency(parts[1], parts[2])
			} else {
				fmt.Println("用法: remove <from> <to>")
			}
		case "adjustments":
			e.showAdjustments()
		case "apply":
			e.applyAdjustments()
		case "save":
			if len(parts) >= 2 {
				e.exportGraph(parts[1])
			} else {
				e.exportGraph(".")
			}
		default:
			fmt.Printf("未知命令: %s. 输入 'help' 查看帮助\n", command)
		}
	}
}

func (e *InteractiveEditor) showHelp() {
	fmt.Println("\n可用命令:")
	fmt.Println("  help          - 显示此帮助信息")
	fmt.Println("  exit          - 退出并保存调整")
	fmt.Println("  quit          - 退出但不保存")
	fmt.Println("  show          - 显示当前依赖图")
	fmt.Println("  cycles        - 检测并显示循环依赖")
	fmt.Println("  list          - 列出所有资源")
	fmt.Println("  add <from> <to>   - 添加依赖关系")
	fmt.Println("  remove <from> <to> - 移除依赖关系")
	fmt.Println("  adjustments   - 显示所有待应用的调整")
	fmt.Println("  apply         - 应用所有调整")
	fmt.Println("  save [dir]    - 导出依赖图到目录")
	fmt.Println()
}

func (e *InteractiveEditor) showGraph() {
	fmt.Println("\n当前依赖图:")
	for from, toList := range e.Graph.Edges {
		fromName := e.getNodeName(from)
		for _, to := range toList {
			toName := e.getNodeName(to)
			fmt.Printf("  %s (%s) -> %s (%s)\n", fromName, from, toName, to)
		}
	}
	if len(e.Graph.Edges) == 0 {
		fmt.Println("  (空图)")
	}
	fmt.Println()
}

func (e *InteractiveEditor) showCycles() {
	result := e.Graph.DetectCycles()
	if result.IsDAG {
		fmt.Println("\n✓ 图是有向无环图(DAG)，没有循环依赖")
	} else {
		fmt.Println("\n⚠️ 检测到循环依赖:")
		for i, cycle := range result.Cycles {
			fmt.Printf("  循环 %d: ", i+1)
			nodeNames := make([]string, len(cycle))
			for j, id := range cycle {
				nodeNames[j] = e.getNodeName(id)
			}
			fmt.Println(strings.Join(nodeNames, " -> "))
		}
		fmt.Println("\n建议:")
		for _, suggestion := range result.Suggestions {
			fmt.Println("  -", suggestion)
		}
	}
	fmt.Println()
}

func (e *InteractiveEditor) listResources() {
	fmt.Println("\n资源列表:")
	for id, node := range e.Graph.Nodes {
		fmt.Printf("  ID: %s, 名称: %s, 类型: %s, 云: %s\n",
			id, node.Name, node.Type, node.Cloud)
	}
	fmt.Println()
}

func (e *InteractiveEditor) addDependency(from, to string) {
	if _, exists := e.Graph.Nodes[from]; !exists {
		fmt.Printf("错误: 资源 '%s' 不存在\n", from)
		return
	}
	if _, exists := e.Graph.Nodes[to]; !exists {
		fmt.Printf("错误: 资源 '%s' 不存在\n", to)
		return
	}

	if edges, exists := e.Graph.Edges[from]; exists {
		for _, edge := range edges {
			if edge == to {
				fmt.Printf("警告: 依赖关系 %s -> %s 已存在\n", from, to)
				return
			}
		}
	}

	e.Adjustments = append(e.Adjustments, DependencyAdjustment{
		From:     from,
		To:       to,
		Action:   "add",
		Reason:   "用户手动添加",
		Suggestion: "",
	})

	fmt.Printf("已添加调整: add %s -> %s\n", from, to)
}

func (e *InteractiveEditor) removeDependency(from, to string) {
	found := false
	if edges, exists := e.Graph.Edges[from]; exists {
		for _, edge := range edges {
			if edge == to {
				found = true
				break
			}
		}
	}

	if !found {
		fmt.Printf("警告: 依赖关系 %s -> %s 不存在\n", from, to)
		return
	}

	e.Adjustments = append(e.Adjustments, DependencyAdjustment{
		From:     from,
		To:       to,
		Action:   "remove",
		Reason:   "用户手动移除",
		Suggestion: "",
	})

	fmt.Printf("已添加调整: remove %s -> %s\n", from, to)
}

func (e *InteractiveEditor) showAdjustments() {
	fmt.Println("\n待应用的调整:")
	if len(e.Adjustments) == 0 {
		fmt.Println("  (无)")
	} else {
		for i, adj := range e.Adjustments {
			fmt.Printf("  %d. %s: %s -> %s (%s)\n",
				i+1, adj.Action, adj.From, adj.To, adj.Reason)
		}
	}
	fmt.Println()
}

func (e *InteractiveEditor) applyAdjustments() {
	fmt.Printf("\n正在应用 %d 个调整...\n", len(e.Adjustments))
	e.Graph.ApplyAdjustments(e.Adjustments)
	
	result := e.Graph.DetectCycles()
	if result.IsDAG {
		fmt.Println("✓ 调整完成，图现在是DAG")
	} else {
		fmt.Println("⚠️ 调整后仍存在循环依赖")
	}
	
	e.Adjustments = []DependencyAdjustment{}
	fmt.Println()
}

func (e *InteractiveEditor) exportGraph(dir string) error {
	if err := e.Graph.ExportDOT(dir); err != nil {
		fmt.Printf("导出DOT失败: %v\n", err)
		return err
	}
	if err := e.Graph.ExportMermaid(dir); err != nil {
		fmt.Printf("导出Mermaid失败: %v\n", err)
		return err
	}
	fmt.Printf("依赖图已导出到目录: %s\n", dir)
	return nil
}

func (e *InteractiveEditor) exit() error {
	if len(e.Adjustments) > 0 {
		fmt.Print("\n有未应用的调整，是否应用并保存? (y/n): ")
		scanner := bufio.NewScanner(os.Stdin)
		scanner.Scan()
		answer := strings.ToLower(strings.TrimSpace(scanner.Text()))
		
		if answer == "y" || answer == "yes" {
			e.applyAdjustments()
			return e.exportGraph(".")
		}
	}
	fmt.Println("退出")
	return nil
}

func (e *InteractiveEditor) getNodeName(id string) string {
	if node, exists := e.Graph.Nodes[id]; exists {
		return node.Name
	}
	return id
}