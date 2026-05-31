#!/usr/bin/env python3
"""
语音命令词识别模型格式说明
用于生成/转换符合本应用要求的 ONNX 模型

模型输入输出规格:
  输入:
    name: input
    shape: [1, 98, 13, 1]  - (batch_size, time_frames, mfcc_coeffs, channels)
    dtype: float32

  输出:
    name: output
    shape: [1, 10]  - (batch_size, num_classes)
    dtype: float32
    内容: 10 个命令词的 logits (未经过 softmax)

10 个命令词索引:
  0: 开机
  1: 关机
  2: 调高音量
  3: 调低音量
  4: 下一首
  5: 上一首
  6: 暂停
  7: 播放
  8: 静音
  9: 取消静音
"""

import numpy as np

# 标签列表（与 Rust 端保持一致）
COMMAND_LABELS = [
    "开机", "关机", "调高音量", "调低音量",
    "下一首", "上一首", "暂停", "播放",
    "静音", "取消静音"
]

# MFCC 参数（与 Rust 端保持一致）
MFCC_CONFIG = {
    "sample_rate": 16000,
    "num_mfcc": 13,
    "num_mel_bins": 40,
    "frame_length_ms": 25,
    "frame_step_ms": 10,
    "fft_size": 512,
    "pre_emphasis": 0.97,
    "lower_frequency": 20,
    "upper_frequency": 8000,
    "num_frames": 98,  # 约 1 秒音频: (1000-25)/10 + 1 ≈ 98
}

# 音频参数（与 Rust 端保持一致）
AUDIO_CONFIG = {
    "sample_rate": 16000,
    "channels": 1,
    "sample_format": "float32",
    "frame_duration_ms": 1000,
    "overlap_ms": 300,
    "samples_per_frame": 16000,  # 16kHz * 1s
}


def print_model_spec():
    """打印模型规格说明"""
    print("="*70)
    print("语音命令词识别 - 模型规格")
    print("="*70)
    print()
    
    print("【输入张量】")
    print(f"  形状: [1, 98, 13, 1]")
    print(f"  含义: [batch_size, time_frames, mfcc_coeffs, channels]")
    print(f"  类型: float32")
    print(f"  范围: [-5.0, 5.0] (归一化的 MFCC 特征)")
    print()
    
    print("【输出张量】")
    print(f"  形状: [1, 10]")
    print(f"  含义: [batch_size, num_classes]")
    print(f"  类型: float32")
    print(f"  内容: 10 个命令词的 logits (未经过 softmax)")
    print()
    
    print("【命令词索引】")
    for i, label in enumerate(COMMAND_LABELS):
        print(f"  {i}: {label}")
    print()
    
    print("【MFCC 参数】")
    for key, value in MFCC_CONFIG.items():
        print(f"  {key}: {value}")
    print()


def create_dummy_model(output_path="models/command_model.onnx"):
    """
    创建一个简单的随机 ONNX 模型作为占位符
    注意: 这只是一个格式正确的假模型，不具备实际识别能力
    
    需要安装: pip install onnx onnxruntime numpy
    """
    try:
        import onnx
        from onnx import helper, TensorProto
    except ImportError:
        print("错误: 请先安装 onnx 包")
        print("  pip install onnx")
        return False
    
    print("正在创建示例 ONNX 模型...")
    
    # 创建输入输出定义
    input_tensor = helper.make_tensor_value_info(
        name="input",
        elem_type=TensorProto.FLOAT,
        shape=[1, 98, 13, 1]
    )
    
    output_tensor = helper.make_tensor_value_info(
        name="output",
        elem_type=TensorProto.FLOAT,
        shape=[1, 10]
    )
    
    # 创建一个简单的恒值节点（输出全零）
    # 实际使用时请替换为训练好的模型
    const_node = helper.make_node(
        "Constant",
        inputs=[],
        outputs=["output_data"],
        value=helper.make_tensor(
            name="const_value",
            data_type=TensorProto.FLOAT,
            dims=[1, 10],
            vals=np.zeros([1, 10], dtype=np.float32).tobytes(),
            raw=True
        )
    )
    
    identity_node = helper.make_node(
        "Identity",
        inputs=["output_data"],
        outputs=["output"]
    )
    
    # 创建图
    graph = helper.make_graph(
        nodes=[const_node, identity_node],
        name="voice_command_dummy",
        inputs=[input_tensor],
        outputs=[output_tensor]
    )
    
    # 创建模型
    model = helper.make_model(graph, producer_name="voice_command_app")
    model.opset_import[0].version = 13
    
    # 保存模型
    import os
    os.makedirs(os.path.dirname(output_path), exist_ok=True)
    onnx.save(model, output_path)
    
    print(f"示例模型已保存到: {output_path}")
    print("注意: 这只是一个格式占位符模型，不具备识别能力")
    print("请将其替换为实际训练好的语音命令词识别模型")
    return True


def verify_model(model_path):
    """验证模型格式是否正确"""
    try:
        import onnxruntime as ort
    except ImportError:
        print("错误: 请先安装 onnxruntime 包")
        print("  pip install onnxruntime")
        return False
    
    print(f"正在验证模型: {model_path}")
    
    try:
        session = ort.InferenceSession(model_path)
    except Exception as e:
        print(f"模型加载失败: {e}")
        return False
    
    # 检查输入
    inputs = session.get_inputs()
    print(f"\n输入: {len(inputs)} 个")
    for inp in inputs:
        print(f"  名称: {inp.name}")
        print(f"  形状: {inp.shape}")
        print(f"  类型: {inp.type}")
        
        expected_shape = [1, 98, 13, 1]
        if inp.shape != expected_shape:
            print(f"  ⚠ 警告: 期望形状 {expected_shape}")
    
    # 检查输出
    outputs = session.get_outputs()
    print(f"\n输出: {len(outputs)} 个")
    for out in outputs:
        print(f"  名称: {out.name}")
        print(f"  形状: {out.shape}")
        print(f"  类型: {out.type}")
        
        expected_shape = [1, 10]
        if out.shape != expected_shape:
            print(f"  ⚠ 警告: 期望形状 {expected_shape}")
    
    # 测试推理
    print("\n测试推理...")
    test_input = np.random.randn(1, 98, 13, 1).astype(np.float32)
    try:
        outputs = session.run(None, {inputs[0].name: test_input})
        print(f"推理成功! 输出形状: {outputs[0].shape}")
        return True
    except Exception as e:
        print(f"推理失败: {e}")
        return False


if __name__ == "__main__":
    import sys
    import os
    
    if len(sys.argv) == 1:
        print_model_spec()
        print("\n用法:")
        print("  python model_spec.py create  - 创建示例模型")
        print("  python model_spec.py verify <model_path>  - 验证模型")
        print()
    elif sys.argv[1] == "create":
        create_dummy_model()
    elif sys.argv[1] == "verify":
        model_path = sys.argv[2] if len(sys.argv) > 2 else "models/command_model.onnx"
        verify_model(model_path)
    else:
        print_model_spec()
