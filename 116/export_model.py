import os
import argparse
import numpy as np
from loguru import logger


def export_mobilenetv3_onnx(output_path: str = "models/mobilenetv3.onnx",
                            feature_dim: int = 256,
                            opset_version: int = 17):
    try:
        import torch
        import torch.nn as nn
        from torchvision import models
    except ImportError:
        logger.error("需要安装 PyTorch 和 torchvision")
        logger.info("运行: pip install torch torchvision")
        raise

    os.makedirs(os.path.dirname(output_path), exist_ok=True)

    logger.info("加载 MobileNetV3-Small 模型...")
    base_model = models.mobilenet_v3_small(weights='IMAGENET1K_V1')
    
    num_features = base_model.classifier[0].in_features
    
    model = nn.Sequential(
        base_model.features,
        base_model.avgpool,
        nn.Flatten(),
        nn.Linear(num_features, feature_dim),
        nn.BatchNorm1d(feature_dim),
        nn.ReLU()
    )
    
    model.eval()
    
    dummy_input = torch.randn(1, 3, 224, 224)
    
    logger.info(f"导出 ONNX 模型到: {output_path}")
    logger.info(f"特征维度: {feature_dim}, Opset版本: {opset_version}")
    
    with torch.no_grad():
        torch.onnx.export(
            model,
            dummy_input,
            output_path,
            export_params=True,
            opset_version=opset_version,
            do_constant_folding=True,
            input_names=['input'],
            output_names=['output'],
            dynamic_axes={
                'input': {0: 'batch_size'},
                'output': {0: 'batch_size'}
            }
        )
    
    logger.info("验证 ONNX 模型...")
    try:
        import onnx
        onnx_model = onnx.load(output_path)
        onnx.checker.check_model(onnx_model)
        logger.info("ONNX 模型验证通过")
        
        onnx.helper.printable_graph(onnx_model.graph)
    except ImportError:
        logger.warning("onnx 包未安装，跳过模型验证")
    except Exception as e:
        logger.error(f"模型验证失败: {e}")
        raise
    
    with torch.no_grad():
        torch_output = model(dummy_input).numpy()
    
    try:
        import onnxruntime as ort
        session = ort.InferenceSession(output_path, providers=['CPUExecutionProvider'])
        onnx_output = session.run(None, {'input': dummy_input.numpy()})[0]
        
        diff = np.max(np.abs(torch_output - onnx_output))
        logger.info(f"PyTorch 与 ONNX 输出最大差异: {diff:.6e}")
        
        if diff < 1e-3:
            logger.info("模型输出一致性验证通过")
        else:
            logger.warning(f"模型输出差异较大，请检查: {diff}")
    except ImportError:
        logger.warning("onnxruntime 未安装，跳过推理一致性验证")
    
    logger.info(f"模型导出完成: {output_path}")
    logger.info(f"文件大小: {os.path.getsize(output_path) / 1024 / 1024:.2f} MB")
    
    return output_path


def main():
    parser = argparse.ArgumentParser(description="导出 MobileNetV3 ONNX 模型")
    parser.add_argument("--output", "-o", type=str, default="models/mobilenetv3.onnx",
                        help="ONNX模型输出路径")
    parser.add_argument("--feature-dim", "-d", type=int, default=256,
                        help="特征向量维度")
    parser.add_argument("--opset", type=int, default=17,
                        help="ONNX opset版本")
    
    args = parser.parse_args()
    
    export_mobilenetv3_onnx(
        output_path=args.output,
        feature_dim=args.feature_dim,
        opset_version=args.opset
    )
    
    print("\n" + "="*60)
    print("模型导出完成！")
    print(f"模型路径: {args.output}")
    print(f"特征维度: {args.feature_dim}")
    print("="*60)
    print("\n下一步:")
    print("1. 设置环境变量: export FEATURE_MODEL_PATH=" + args.output)
    print("2. 或者修改 config.py 中的 deep_feature_config.model_path")
    print("3. 运行服务: python main.py")


if __name__ == "__main__":
    main()
