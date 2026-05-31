package risk

import (
	"math"
	"math/rand"
	"sort"
	"sync"
)

type IsolationForest struct {
	mu       sync.RWMutex
	trees    []*IsolationTree
	numTrees int
	sampleSize int
	maxDepth int
}

type IsolationTree struct {
	root *Node
}

type Node struct {
	splitPoint    float64
	splitFeature  int
	left          *Node
	right         *Node
	size          int
	isLeaf        bool
}

type DataPoint struct {
	Features []float64
	Label    string
}

func NewIsolationForest(numTrees, sampleSize int) *IsolationForest {
	return &IsolationForest{
		trees:      make([]*IsolationTree, numTrees),
		numTrees:   numTrees,
		sampleSize: sampleSize,
		maxDepth:   int(math.Ceil(math.Log2(float64(sampleSize)))),
	}
}

func (f *IsolationForest) Train(data []DataPoint) {
	f.mu.Lock()
	defer f.mu.Unlock()

	n := len(data)
	if f.sampleSize > n {
		f.sampleSize = n
	}
	f.maxDepth = int(math.Ceil(math.Log2(float64(f.sampleSize))))

	for i := 0; i < f.numTrees; i++ {
		sample := f.randomSubsample(data)
		f.trees[i] = f.buildTree(sample, 0)
	}
}

func (f *IsolationForest) randomSubsample(data []DataPoint) []DataPoint {
	n := len(data)
	indices := make([]int, n)
	for i := range indices {
		indices[i] = i
	}
	rand.Shuffle(n, func(i, j int) {
		indices[i], indices[j] = indices[j], indices[i]
	})

	sampleSize := f.sampleSize
	if sampleSize > n {
		sampleSize = n
	}

	sample := make([]DataPoint, sampleSize)
	for i := 0; i < sampleSize; i++ {
		sample[i] = data[indices[i]]
	}
	return sample
}

func (f *IsolationForest) buildTree(data []DataPoint, depth int) *IsolationTree {
	node := f.buildNode(data, depth)
	return &IsolationTree{root: node}
}

func (f *IsolationForest) buildNode(data []DataPoint, depth int) *Node {
	n := len(data)
	if depth >= f.maxDepth || n <= 1 {
		return &Node{
			size:   n,
			isLeaf: true,
		}
	}

	numFeatures := len(data[0].Features)
	bestFeature := 0
	bestSplit := 0.0
	bestScore := math.Inf(1)

	for feature := 0; feature < numFeatures; feature++ {
		values := make([]float64, n)
		for i, dp := range data {
			values[i] = dp.Features[feature]
		}
		sort.Float64s(values)

		minVal := values[0]
		maxVal := values[n-1]

		if maxVal-minVal < 1e-10 {
			continue
		}

		for attempts := 0; attempts < 5; attempts++ {
			splitVal := minVal + rand.Float64()*(maxVal-minVal)
			leftCount := 0
			for _, v := range values {
				if v < splitVal {
					leftCount++
				}
			}
			rightCount := n - leftCount

			if leftCount == 0 || rightCount == 0 {
				continue
			}

			score := float64(leftCount*rightCount) / float64(n*n)
			if score < bestScore {
				bestScore = score
				bestFeature = feature
				bestSplit = splitVal
			}
		}
	}

	if bestScore == math.Inf(1) {
		return &Node{
			size:   n,
			isLeaf: true,
		}
	}

	var leftData, rightData []DataPoint
	for _, dp := range data {
		if dp.Features[bestFeature] < bestSplit {
			leftData = append(leftData, dp)
		} else {
			rightData = append(rightData, dp)
		}
	}

	return &Node{
		splitPoint:   bestSplit,
		splitFeature: bestFeature,
		left:         f.buildNode(leftData, depth+1),
		right:        f.buildNode(rightData, depth+1),
	}
}

func (f *IsolationForest) AnomalyScore(point DataPoint) float64 {
	f.mu.RLock()
	defer f.mu.RUnlock()

	var totalDepth float64
	for _, tree := range f.trees {
		totalDepth += float64(pathLength(tree.root, point, 0))
	}

	avgPathLength := totalDepth / float64(f.numTrees)
	c := averagePathLength(float64(f.sampleSize))

	score := math.Pow(2, -avgPathLength/c)
	return score * 100
}

func pathLength(node *Node, point DataPoint, currentDepth int) int {
	if node.isLeaf {
		return currentDepth + adjustment(node.size)
	}

	if point.Features[node.splitFeature] < node.splitPoint {
		return pathLength(node.left, point, currentDepth+1)
	}
	return pathLength(node.right, point, currentDepth+1)
}

func adjustment(size int) int {
	if size <= 1 {
		return 0
	}
	return int(averagePathLength(float64(size)))
}

func averagePathLength(n float64) float64 {
	if n <= 1 {
		return 0
	}
	return 2 * (harmonicNumber(n-1) - (n-1)/n)
}

func harmonicNumber(n float64) float64 {
	return math.Log(n) + 0.5772156649
}

type RiskLevel int

const (
	RiskLow      RiskLevel = 0
	RiskMedium   RiskLevel = 1
	RiskHigh     RiskLevel = 2
	RiskCritical RiskLevel = 3
)

func ClassifyRisk(score float64) RiskLevel {
	switch {
	case score < 30:
		return RiskLow
	case score < 60:
		return RiskMedium
	case score < 85:
		return RiskHigh
	default:
		return RiskCritical
	}
}

func (rl RiskLevel) String() string {
	switch rl {
	case RiskLow:
		return "low"
	case RiskMedium:
		return "medium"
	case RiskHigh:
		return "high"
	case RiskCritical:
		return "critical"
	default:
		return "unknown"
	}
}
