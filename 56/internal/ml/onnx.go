package ml

import (
	"fmt"
	"os"
	"sync"
)

var (
	onnxOnce    sync.Once
	onnxSession interface{}
	onnxErr     error
)

func loadModel(modelPath string) (bool, error) {
	if _, err := os.Stat(modelPath); os.IsNotExist(err) {
		return false, fmt.Errorf("model file not found: %s", modelPath)
	}

	onnxOnce.Do(func() {
		onnxSession, onnxErr = createONNXSession(modelPath)
	})

	if onnxErr != nil {
		return false, onnxErr
	}
	return onnxSession != nil, nil
}

func createONNXSession(modelPath string) (interface{}, error) {
	// Try to load using ONNX Runtime Go bindings
	// This requires the onnxruntime C library to be installed.
	// If the library is not available, we gracefully fall back to heuristic scoring.
	session, err := tryLoadONNX(modelPath)
	if err != nil {
		return nil, fmt.Errorf("onnx runtime not available: %w", err)
	}
	return session, nil
}

func runInference(features []float32) (float64, error) {
	if onnxSession == nil {
		return 0, fmt.Errorf("no onnx session loaded")
	}
	score, err := inferONNX(onnxSession, features)
	if err != nil {
		return 0, err
	}
	return score, nil
}

// tryLoadONNX attempts to load the ONNX model.
// Returns (session, nil) on success, (nil, error) on failure.
// The actual ONNX Runtime integration is isolated here to allow
// graceful fallback when the C library is not installed.
func tryLoadONNX(modelPath string) (interface{}, error) {
	// Integration point for github.com/microsoft/onnxruntime-go
	//
	// Example:
	//   import ort "github.com/microsoft/onnxruntime-go"
	//   env, _ := ort.NewEnvironment(ort.LogLevelWarning, "iprep")
	//   sess, _ := env.NewSession(modelPath, &ort.SessionOptions{...})
	//   return sess, nil
	//
	// For now, we return a placeholder that indicates ONNX Runtime
	// is not linked. The scorer will use heuristic scoring.
	return nil, fmt.Errorf("onnxruntime Go bindings not compiled in; use heuristic scoring")
}

func inferONNX(session interface{}, features []float32) (float64, error) {
	// Integration point for actual inference:
	//   inputNames := session.InputNames()
	//   outputNames := session.OutputNames()
	//   inputs := map[string]interface{}{inputNames[0]: features}
	//   outputs, err := session.Run(inputs, outputNames)
	//   if err != nil { return 0, err }
	//   return float64(outputs[outputNames[0]][0].(float32)), nil
	return 0, fmt.Errorf("onnx inference not implemented")
}
