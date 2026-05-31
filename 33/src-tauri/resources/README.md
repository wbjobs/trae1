# Resources

Place your trained ONNX model here, named `model.onnx`.

Expected input shape: `1x3x224x224` (NCHW, float32).
Expected output shape: `1xN` where `N` is the number of gesture classes.

The model should output raw logits; the backend will automatically apply softmax.
