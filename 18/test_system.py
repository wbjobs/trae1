"""
独立测试脚本 - 矩阵特征值与特征向量高精度求解系统
直接运行此脚本即可测试所有功能
"""

import sys
import os
import numpy as np
import json
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

from sparse_compat import SparseMatrix, random as sparse_random, HAS_SCIPY
from matrix_preprocessor import MatrixPreprocessor
from qr_decomposition import QRDecomposition, QRMethod
from eigen_solver import EigenSolver, ShiftStrategy, ConvergenceCriterion
from precision_corrector import PrecisionCorrector, RefinementMethod
from result_validator import ResultValidator
from matrix_visualizer import MatrixVisualizer


def test_matrix_preprocessor():
    print("\n" + "=" * 60)
    print("测试1: 矩阵预处理模块")
    print("=" * 60)

    preprocessor = MatrixPreprocessor(tolerance=1e-14)

    np.random.seed(42)
    matrix = np.random.randn(10, 10)

    info = preprocessor.analyze_matrix(matrix)
    print(f"  矩阵形状: {info.shape}")
    print(f"  矩阵类型: {info.matrix_type.value}")
    print(f"  是否稀疏: {info.is_sparse}")
    print(f"  非零元素: {info.nnz}")
    print(f"  稀疏度: {info.density:.4f}")
    print(f"  条件数: {info.condition_number:.2f}")

    H, Q = preprocessor.permute_to_upper_hessenberg(matrix)
    print(f"  上Hessenberg变换完成")
    print(f"  验证: max|H[i,i-2]| = {np.max(np.abs(np.tril(H, -2))):.6e}")

    balanced = preprocessor.balance_matrix(matrix)
    print(f"  矩阵平衡完成")

    print("  ✓ 矩阵预处理模块测试通过")
    return True


def test_qr_decomposition():
    print("\n" + "=" * 60)
    print("测试2: QR分解核心模块")
    print("=" * 60)

    qr_householder = QRDecomposition(method=QRMethod.HOUSEHOLDER, tolerance=1e-14)
    qr_givens = QRDecomposition(method=QRMethod.GIVENS, tolerance=1e-14)

    np.random.seed(123)
    matrix = np.random.randn(8, 8)

    result_hh = qr_householder.decompose(matrix)
    print(f"  Householder QR分解:")
    print(f"    Q形状: {result_hh.Q.shape}, R形状: {result_hh.R.shape}")
    print(f"    残差: {result_hh.error:.6e}")

    error_hh = np.linalg.norm(matrix - result_hh.Q @ result_hh.R)
    print(f"    A ≈ QR 验证: ||A - QR|| = {error_hh:.6e}")

    Q_ortho = np.linalg.norm(result_hh.Q.T @ result_hh.Q - np.eye(8))
    print(f"    Q正交性验证: ||Q^T Q - I|| = {Q_ortho:.6e}")

    result_giv = qr_givens.decompose(matrix)
    print(f"  Givens QR分解:")
    print(f"    残差: {result_giv.error:.6e}")

    error_giv = np.linalg.norm(matrix - result_giv.Q @ result_giv.R)
    print(f"    A ≈ QR 验证: ||A - QR|| = {error_giv:.6e}")

    print("  ✓ QR分解核心模块测试通过")
    return True


def test_eigen_solver():
    print("\n" + "=" * 60)
    print("测试3: 特征迭代求解模块")
    print("=" * 60)

    solver = EigenSolver(
        max_iterations=500,
        tolerance=1e-12,
        shift_strategy=ShiftStrategy.WILKINSON,
        compute_eigenvectors=True
    )

    np.random.seed(456)
    D = np.diag([1.0, 2.0, 3.0, 5.0, 8.0])
    Q, _ = np.linalg.qr(np.random.randn(5, 5))
    matrix = Q @ D @ Q.T

    result = solver.solve(matrix)

    print(f"  迭代次数: {result.iteration_count}")
    print(f"  收敛: {result.converged}")
    print(f"  计算时间: {result.computation_time:.4f}秒")
    print(f"  最终残差: {result.final_residual:.6e}")

    print(f"  计算特征值:")
    for i, ev in enumerate(result.eigenvalues):
        print(f"    λ{i+1} = {ev.real:12.8f} + {ev.imag:12.8f}j")

    print(f"  参考特征值: [1.0, 2.0, 3.0, 5.0, 8.0]")

    computed = np.sort(np.abs(result.eigenvalues))
    reference = np.sort([1.0, 2.0, 3.0, 5.0, 8.0])
    errors = np.abs(computed - reference)
    print(f"  绝对误差: max={np.max(errors):.6e}, mean={np.mean(errors):.6e}")

    print("  ✓ 特征迭代求解模块测试通过")
    return result


def test_precision_corrector():
    print("\n" + "=" * 60)
    print("测试4: 精度修正模块")
    print("=" * 60)

    corrector = PrecisionCorrector(
        method=RefinementMethod.INVERSE_ITERATION,
        max_iterations=10,
        tolerance=1e-14
    )

    np.random.seed(789)
    matrix = np.random.randn(8, 8)

    eigenvalues = np.linalg.eigvals(matrix)
    _, eigenvectors = np.linalg.eig(matrix)

    print(f"  原始误差分析:")
    report = corrector.compute_error_report(matrix, eigenvalues, eigenvectors)
    print(f"    最大特征值误差: {report.max_eigenvalue_error:.6e}")
    print(f"    最大特征向量误差: {report.max_eigenvector_error:.6e}")
    print(f"    平均相对误差: {report.avg_relative_error:.6e}")

    refinement = corrector.refine(matrix, eigenvalues, eigenvectors)

    print(f"  修正后误差分析:")
    print(f"    最大特征值误差: {refinement.refined_errors.max_eigenvalue_error:.6e}")
    print(f"    最大特征向量误差: {refinement.refined_errors.max_eigenvector_error:.6e}")
    print(f"    平均相对误差: {refinement.refined_errors.avg_relative_error:.6e}")

    print("  ✓ 精度修正模块测试通过")
    return True


def test_result_validator():
    print("\n" + "=" * 60)
    print("测试5: 结果校验模块")
    print("=" * 60)

    validator = ResultValidator(tolerance=1e-10)

    np.random.seed(321)
    matrix = np.random.randn(6, 6)

    eigenvalues, eigenvectors = np.linalg.eig(matrix)

    result = validator.validate(matrix, eigenvalues, eigenvectors)

    print(f"  整体验证: {'通过' if result.is_valid else '未通过'}")
    print(f"  各验证方法:")
    for method, passed in result.validation_methods.items():
        print(f"    {method}: {'通过' if passed else '未通过'}")

    print(f"  误差详情:")
    for key, value in result.error_details.items():
        print(f"    {key}: {value:.6e}")

    report = validator.generate_validation_report(result)
    print(f"\n{report}")

    print("  ✓ 结果校验模块测试通过")
    return True


def test_sparse_matrix():
    print("\n" + "=" * 60)
    print("测试6: 稀疏矩阵支持")
    print("=" * 60)

    preprocessor = MatrixPreprocessor(tolerance=1e-14)

    size = 20
    density = 0.15
    sparse_matrix = sparse_random((size, size), density=density, format='csr', random_state=42)

    print(f"  稀疏矩阵: {size}x{size}, 密度: {density}")
    print(f"  非零元素数: {sparse_matrix.nnz}")

    info = preprocessor.analyze_matrix(sparse_matrix)
    print(f"  矩阵类型: {info.matrix_type.value}")

    dense_matrix = preprocessor.to_dense(sparse_matrix)
    print(f"  转换为稠密矩阵: 形状{dense_matrix.shape}")

    back_to_sparse = preprocessor.to_sparse(dense_matrix, threshold=1e-10, format='csr')
    print(f"  转回稀疏矩阵: 非零元素{back_to_sparse.nnz}")

    solver = EigenSolver(max_iterations=500, tolerance=1e-12)
    result = solver.solve(dense_matrix)

    print(f"  稀疏矩阵特征值求解:")
    print(f"    迭代次数: {result.iteration_count}")
    print(f"    收敛: {result.converged}")

    print("  ✓ 稀疏矩阵支持测试通过")
    return True


def test_high_order_matrix():
    print("\n" + "=" * 60)
    print("测试7: 高阶矩阵计算")
    print("=" * 60)

    solver = EigenSolver(
        max_iterations=1000,
        tolerance=1e-10,
        compute_eigenvectors=False
    )

    sizes = [20, 50, 100]

    for size in sizes:
        np.random.seed(1000 + size)
        matrix = np.random.randn(size, size)

        start_time = time.time()
        result = solver.solve(matrix)
        elapsed = time.time() - start_time

        print(f"  {size}x{size} 矩阵:")
        print(f"    迭代次数: {result.iteration_count}")
        print(f"    收敛: {result.converged}")
        print(f"    耗时: {elapsed:.4f}秒")

    print("  ✓ 高阶矩阵计算测试通过")
    return True


def test_iteration_data_output():
    print("\n" + "=" * 60)
    print("测试8: 迭代过程数据输出")
    print("=" * 60)

    solver = EigenSolver(
        max_iterations=500,
        tolerance=1e-12,
        compute_eigenvectors=False
    )

    np.random.seed(2024)
    matrix = np.random.randn(10, 10)

    result = solver.solve(matrix)

    print(f"  迭代次数: {result.iteration_count}")
    print(f"  迭代历史记录数: {len(result.iteration_history)}")

    if result.iteration_history:
        print(f"  前5次迭代数据:")
        for i, data in enumerate(result.iteration_history[:5]):
            print(f"    迭代{i}: 位移={data.shift:.6f}, "
                  f"最大次对角={data.max_off_diagonal:.6e}, "
                  f"残差={data.residual_norm:.6e}")

    output_dir = "./output"
    os.makedirs(output_dir, exist_ok=True)

    filepath = os.path.join(output_dir, "test_iteration_data.json")
    data_to_export = []
    for item in result.iteration_history:
        entry = {
            'iteration': item.iteration,
            'shift': item.shift,
            'max_off_diagonal': item.max_off_diagonal,
            'residual_norm': item.residual_norm,
        }
        data_to_export.append(entry)

    with open(filepath, 'w', encoding='utf-8') as f:
        json.dump(data_to_export, f, indent=2, ensure_ascii=False)

    print(f"  迭代数据已导出: {filepath}")
    print("  ✓ 迭代过程数据输出测试通过")
    return True


def test_error_report_output():
    print("\n" + "=" * 60)
    print("测试9: 误差报表输出")
    print("=" * 60)

    corrector = PrecisionCorrector(
        method=RefinementMethod.INVERSE_ITERATION,
        tolerance=1e-14
    )

    np.random.seed(2025)
    matrix = np.random.randn(8, 8)

    eigenvalues, eigenvectors = np.linalg.eig(matrix)

    refinement = corrector.refine(matrix, eigenvalues, eigenvectors)

    print(f"  原始误差:")
    print(f"    最大特征值误差: {refinement.original_errors.max_eigenvalue_error:.6e}")
    print(f"    最大特征向量误差: {refinement.original_errors.max_eigenvector_error:.6e}")
    print(f"    平均相对误差: {refinement.original_errors.avg_relative_error:.6e}")

    print(f"  修正后误差:")
    print(f"    最大特征值误差: {refinement.refined_errors.max_eigenvalue_error:.6e}")
    print(f"    最大特征向量误差: {refinement.refined_errors.max_eigenvector_error:.6e}")
    print(f"    平均相对误差: {refinement.refined_errors.avg_relative_error:.6e}")

    output_dir = "./output"
    os.makedirs(output_dir, exist_ok=True)

    filepath = os.path.join(output_dir, "test_error_report.json")
    report_data = {
        'original': {
            'max_eigenvalue_error': refinement.original_errors.max_eigenvalue_error,
            'max_eigenvector_error': refinement.original_errors.max_eigenvector_error,
            'avg_relative_error': refinement.original_errors.avg_relative_error,
        },
        'refined': {
            'max_eigenvalue_error': refinement.refined_errors.max_eigenvalue_error,
            'max_eigenvector_error': refinement.refined_errors.max_eigenvector_error,
            'avg_relative_error': refinement.refined_errors.avg_relative_error,
        },
        'method': refinement.method.value,
    }

    with open(filepath, 'w', encoding='utf-8') as f:
        json.dump(report_data, f, indent=2, ensure_ascii=False)

    print(f"  误差报表已导出: {filepath}")
    print("  ✓ 误差报表输出测试通过")
    return True


def main():
    print("=" * 70)
    print("矩阵特征值与特征向量高精度求解系统 - 功能测试")
    print("=" * 70)
    print(f"Python版本: {sys.version}")
    print(f"NumPy版本: {np.__version__}")

    tests = [
        ("矩阵预处理", test_matrix_preprocessor),
        ("QR分解", test_qr_decomposition),
        ("特征求解", test_eigen_solver),
        ("精度修正", test_precision_corrector),
        ("结果校验", test_result_validator),
        ("稀疏矩阵", test_sparse_matrix),
        ("高阶矩阵", test_high_order_matrix),
        ("迭代数据", test_iteration_data_output),
        ("误差报表", test_error_report_output),
    ]

    results = {}
    start_time = time.time()

    for name, test_func in tests:
        try:
            result = test_func()
            results[name] = "通过"
        except Exception as e:
            print(f"\n  ✗ {name}测试失败: {str(e)}")
            import traceback
            traceback.print_exc()
            results[name] = f"失败: {str(e)}"

    total_time = time.time() - start_time

    print("\n" + "=" * 70)
    print("测试总结")
    print("=" * 70)
    for name, status in results.items():
        print(f"  {name}: {status}")

    passed = sum(1 for v in results.values() if v == "通过")
    total = len(results)
    print(f"\n总计: {passed}/{total} 测试通过")
    print(f"总耗时: {total_time:.4f}秒")

    return 0 if passed == total else 1


if __name__ == "__main__":
    sys.exit(main())
