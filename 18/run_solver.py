"""
简化运行脚本 - 矩阵特征值与特征向量高精度求解系统
不包含可视化，专注于核心功能验证
"""

import sys
import os
import numpy as np
import json
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

from matrix_preprocessor import MatrixPreprocessor
from qr_decomposition import QRDecomposition, QRMethod
from eigen_solver import EigenSolver, ShiftStrategy
from precision_corrector import PrecisionCorrector, RefinementMethod
from result_validator import ResultValidator
from sparse_compat import sparse_random, issparse


def create_test_matrix(matrix_type="random", size=10, seed=None):
    if seed is not None:
        np.random.seed(seed)

    if matrix_type == "random":
        return np.random.randn(size, size)
    elif matrix_type == "symmetric":
        A = np.random.randn(size, size)
        return (A + A.T) / 2
    elif matrix_type == "diagonal":
        return np.diag(np.random.randn(size) * 10)
    elif matrix_type == "tridiagonal":
        A = np.zeros((size, size))
        A[np.diag_indices(size)] = np.random.randn(size) * 10
        for i in range(size - 1):
            A[i, i + 1] = np.random.randn()
            A[i + 1, i] = A[i, i + 1]
        return A
    elif matrix_type == "upper_hessenberg":
        A = np.zeros((size, size))
        for i in range(size):
            for j in range(max(0, i - 1), size):
                A[i, j] = np.random.randn()
        return A
    elif matrix_type == "sparse":
        return sparse_random((size, size), density=0.15, format='csr', random_state=seed).toarray()
    elif matrix_type == "known_eigenvalues":
        eigenvalues = np.array([1.0, 2.0, 3.0, 5.0, 8.0, 13.0])[:size]
        D = np.diag(eigenvalues)
        Q = np.linalg.qr(np.random.randn(size, size))[0]
        return Q @ D @ Q.T
    else:
        return np.random.randn(size, size)


def main():
    print("=" * 70)
    print("矩阵特征值与特征向量高精度求解系统")
    print("=" * 70)
    print(f"Python: {sys.version.split()[0]}, NumPy: {np.__version__}")
    print()

    output_dir = "./output"
    os.makedirs(output_dir, exist_ok=True)

    test_cases = [
        ("对称矩阵", "symmetric", 6, 42),
        ("三对角矩阵", "tridiagonal", 8, 123),
        ("上Hessenberg矩阵", "upper_hessenberg", 6, 456),
        ("已知特征值矩阵", "known_eigenvalues", 6, 789),
        ("随机矩阵", "random", 5, 2024),
        ("稀疏矩阵", "sparse", 10, 555),
    ]

    for name, mtype, size, seed in test_cases:
        print(f"\n{'='*60}")
        print(f"测试: {name} ({size}x{size})")
        print(f"{'='*60}")

        matrix = create_test_matrix(mtype, size, seed)

        if hasattr(matrix, 'toarray'):
            matrix = matrix.toarray()

        print(f"矩阵条件数: {np.linalg.cond(matrix):.2e}")
        print(f"非零元素数: {np.count_nonzero(matrix)}")

        # Step 1: 矩阵预处理
        preprocessor = MatrixPreprocessor(tolerance=1e-14)
        info = preprocessor.analyze_matrix(matrix)
        print(f"矩阵类型: {info.matrix_type.value}")

        H, Q_hess = preprocessor.permute_to_upper_hessenberg(matrix)
        print(f"Hessenberg变换: max|H[i,i-2]| = {np.max(np.abs(np.tril(H, -2))):.6e}")

        # Step 2: QR分解验证
        qr_solver = QRDecomposition(method=QRMethod.HOUSEHOLDER, tolerance=1e-14)
        qr_result = qr_solver.decompose(matrix)
        qr_error = np.linalg.norm(matrix - qr_result.Q @ qr_result.R)
        print(f"QR分解验证: ||A - QR|| = {qr_error:.6e}")

        # Step 3: 特征值求解
        solver = EigenSolver(
            max_iterations=500,
            tolerance=1e-12,
            shift_strategy=ShiftStrategy.WILKINSON,
            compute_eigenvectors=True
        )
        eigen_result = solver.solve(matrix)

        print(f"\n特征值求解结果:")
        print(f"  迭代次数: {eigen_result.iteration_count}")
        print(f"  收敛: {eigen_result.converged}")
        print(f"  计算时间: {eigen_result.computation_time:.4f}秒")

        print(f"\n特征值:")
        for i, ev in enumerate(eigen_result.eigenvalues):
            if abs(ev.imag) < 1e-10:
                print(f"  λ{i+1:2d} = {ev.real:15.10f}")
            else:
                print(f"  λ{i+1:2d} = {ev.real:10.6f} + {ev.imag:10.6f}j")

        # Step 4: 精度修正
        corrector = PrecisionCorrector(
            method=RefinementMethod.INVERSE_ITERATION,
            tolerance=1e-14
        )
        refinement = corrector.refine(
            matrix,
            eigen_result.eigenvalues,
            eigen_result.eigenvectors
        )

        print(f"\n精度修正:")
        print(f"  原始平均相对误差: {refinement.original_errors.avg_relative_error:.6e}")
        print(f"  修正后平均相对误差: {refinement.refined_errors.avg_relative_error:.6e}")

        # Step 5: 结果校验
        validator = ResultValidator(tolerance=1e-10)
        validation = validator.validate(
            matrix,
            refinement.eigenvalues_refined,
            refinement.eigenvectors_refined
        )

        print(f"\n结果校验:")
        print(f"  整体: {'通过' if validation.is_valid else '未通过'}")
        for method, passed in validation.validation_methods.items():
            print(f"    {method}: {'通过' if passed else '未通过'}")

        # Step 6: 导出迭代数据
        iter_filename = os.path.join(output_dir, f"iteration_{name}.json")
        iter_data = []
        for item in eigen_result.iteration_history:
            entry = {
                'iteration': item.iteration,
                'shift': item.shift,
                'max_off_diagonal': item.max_off_diagonal,
                'residual_norm': item.residual_norm,
            }
            iter_data.append(entry)
        with open(iter_filename, 'w', encoding='utf-8') as f:
            json.dump(iter_data, f, indent=2)
        print(f"\n迭代数据已导出: {iter_filename}")

        # Step 7: 导出误差报表
        error_filename = os.path.join(output_dir, f"error_report_{name}.json")
        error_data = {
            'original': {
                'max_eigenvalue_error': float(refinement.original_errors.max_eigenvalue_error),
                'max_eigenvector_error': float(refinement.original_errors.max_eigenvector_error),
                'avg_relative_error': float(refinement.original_errors.avg_relative_error),
            },
            'refined': {
                'max_eigenvalue_error': float(refinement.refined_errors.max_eigenvalue_error),
                'max_eigenvector_error': float(refinement.refined_errors.max_eigenvector_error),
                'avg_relative_error': float(refinement.refined_errors.avg_relative_error),
            },
            'validation': {
                'is_valid': bool(validation.is_valid),
                'methods': {k: bool(v) for k, v in validation.validation_methods.items()},
                'errors': {k: float(v) for k, v in validation.error_details.items()},
            }
        }
        with open(error_filename, 'w', encoding='utf-8') as f:
            json.dump(error_data, f, indent=2)
        print(f"误差报表已导出: {error_filename}")

    # 高阶矩阵测试
    print(f"\n{'='*60}")
    print("高阶矩阵性能测试")
    print(f"{'='*60}")

    for size in [20, 50, 100]:
        np.random.seed(1000 + size)
        matrix = np.random.randn(size, size)

        solver = EigenSolver(
            max_iterations=1000,
            tolerance=1e-10,
            compute_eigenvectors=False
        )

        start = time.time()
        result = solver.solve(matrix)
        elapsed = time.time() - start

        print(f"  {size:3d}x{size:3d}: 迭代={result.iteration_count:3d}, "
              f"收敛={result.converged}, 耗时={elapsed:.4f}秒")

    print(f"\n{'='*70}")
    print("所有测试完成")
    print(f"输出目录: {os.path.abspath(output_dir)}")
    print(f"{'='*70}")


if __name__ == "__main__":
    main()
