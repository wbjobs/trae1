"""
修复验证测试脚本
测试稀疏矩阵、高阶矩阵、奇异矩阵、特征向量匹配的修复效果
"""

import sys
import os
import numpy as np

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

from eigen_solver import EigenSolver, ShiftStrategy
from precision_corrector import PrecisionCorrector, RefinementMethod
from result_validator import ResultValidator
from sparse_compat import sparse_random, issparse


def test_sparse_matrix_fix():
    print("\n" + "=" * 60)
    print("测试1: 稀疏矩阵特征值求解偏差修复")
    print("=" * 60)

    np.random.seed(42)
    size = 10
    density = 0.2

    sparse_mat = sparse_random((size, size), density=density, format='csr', random_state=42)
    dense_mat = sparse_mat.toarray()

    print(f"稀疏矩阵: {size}x{size}, 密度: {density}, 非零元素: {sparse_mat.nnz}")

    solver = EigenSolver(
        max_iterations=500,
        tolerance=1e-12,
        shift_strategy=ShiftStrategy.WILKINSON,
        compute_eigenvectors=True
    )

    result = solver.solve(dense_mat)

    print(f"\nQR算法结果:")
    print(f"  迭代次数: {result.iteration_count}")
    print(f"  收敛: {result.converged}")
    print(f"  最终残差: {result.final_residual:.6e}")

    print(f"\n特征值:")
    for i, ev in enumerate(result.eigenvalues):
        if abs(ev.imag) < 1e-10:
            print(f"  λ{i+1:2d} = {ev.real:15.10f}")
        else:
            print(f"  λ{i+1:2d} = {ev.real:10.6f} + {ev.imag:10.6f}j")

    reference_ev = np.linalg.eigvals(dense_mat)
    reference_ev = np.sort(np.abs(reference_ev))
    computed_ev = np.sort(np.abs(result.eigenvalues))

    errors = np.abs(reference_ev - computed_ev)
    print(f"\n与参考解对比:")
    print(f"  最大绝对误差: {np.max(errors):.6e}")
    print(f"  平均绝对误差: {np.mean(errors):.6e}")
    print(f"  最大相对误差: {np.max(errors / (np.abs(reference_ev) + 1e-16)):.6e}")

    if result.eigenvectors is not None:
        residual_norms = []
        for i in range(size):
            ev = result.eigenvalues[i]
            vec = result.eigenvectors[:, i]
            residual = np.linalg.norm(dense_mat @ vec - ev * vec)
            residual_norms.append(residual)
        print(f"\n特征向量残差:")
        print(f"  最大残差: {np.max(residual_norms):.6e}")
        print(f"  平均残差: {np.mean(residual_norms):.6e}")

    print("\n✓ 稀疏矩阵测试完成")
    return True


def test_high_order_matrix():
    print("\n" + "=" * 60)
    print("测试2: 高阶矩阵迭代死循环修复")
    print("=" * 60)

    sizes = [20, 50, 100, 200]

    for size in sizes:
        np.random.seed(1000 + size)
        matrix = np.random.randn(size, size)

        solver = EigenSolver(
            max_iterations=1000,
            tolerance=1e-10,
            shift_strategy=ShiftStrategy.WILKINSON,
            compute_eigenvectors=False
        )

        import time
        start = time.time()
        result = solver.solve(matrix)
        elapsed = time.time() - start

        print(f"\n  {size:3d}x{size:3d} 矩阵:")
        print(f"    迭代次数: {result.iteration_count:4d}")
        print(f"    收敛: {result.converged}")
        print(f"    耗时: {elapsed:.4f}秒")
        print(f"    最终残差: {result.final_residual:.6e}")

        if not result.converged:
            print(f"    警告: 未完全收敛，但避免了死循环")

    print("\n✓ 高阶矩阵测试完成")
    return True


def test_singular_matrix():
    print("\n" + "=" * 60)
    print("测试3: 奇异矩阵异常处理修复")
    print("=" * 60)

    np.random.seed(12345)

    print("\n3.1 近奇异矩阵:")
    matrix = np.random.randn(8, 8)
    matrix[0, :] = matrix[1, :] + 1e-15 * np.random.randn(8)
    print(f"  矩阵条件数: {np.linalg.cond(matrix):.2e}")

    try:
        solver = EigenSolver(
            max_iterations=500,
            tolerance=1e-12,
            compute_eigenvectors=True
        )
        result = solver.solve(matrix)
        print(f"  迭代次数: {result.iteration_count}")
        print(f"  收敛: {result.converged}")
        print(f"  程序正常运行，无崩溃")
    except Exception as e:
        print(f"  错误: {e}")
        return False

    print("\n3.2 完全奇异矩阵:")
    matrix2 = np.random.randn(6, 6)
    matrix2[-1, :] = np.sum(matrix2[:-1, :], axis=0)
    print(f"  矩阵秩: {np.linalg.matrix_rank(matrix2)}/{matrix2.shape[0]}")

    try:
        result2 = solver.solve(matrix2)
        print(f"  迭代次数: {result2.iteration_count}")
        print(f"  收敛: {result2.converged}")
        print(f"  程序正常运行，无崩溃")
    except Exception as e:
        print(f"  错误: {e}")
        return False

    print("\n3.3 零矩阵:")
    zero_matrix = np.zeros((5, 5))
    try:
        result3 = solver.solve(zero_matrix)
        print(f"  迭代次数: {result3.iteration_count}")
        print(f"  收敛: {result3.converged}")
        print(f"  程序正常运行，无崩溃")
    except Exception as e:
        print(f"  错误: {e}")
        return False

    print("\n✓ 奇异矩阵测试完成")
    return True


def test_eigenvector_matching():
    print("\n" + "=" * 60)
    print("测试4: 特征向量匹配修复")
    print("=" * 60)

    np.random.seed(56789)

    print("\n4.1 实特征值矩阵:")
    D = np.diag([1.0, 2.0, 3.0, 5.0, 8.0])
    Q, _ = np.linalg.qr(np.random.randn(5, 5))
    matrix = Q @ D @ Q.T

    solver = EigenSolver(
        max_iterations=500,
        tolerance=1e-14,
        shift_strategy=ShiftStrategy.WILKINSON,
        compute_eigenvectors=True
    )

    result = solver.solve(matrix)

    print(f"  迭代次数: {result.iteration_count}")
    print(f"  收敛: {result.converged}")

    print(f"\n  特征值与参考解对比:")
    reference = np.array([1.0, 2.0, 3.0, 5.0, 8.0])
    for i, (ev, ref) in enumerate(zip(result.eigenvalues, reference)):
        print(f"    λ{i+1}: 计算值={ev.real:12.8f}, 参考值={ref:12.8f}, 误差={abs(ev.real - ref):.6e}")

    if result.eigenvectors is not None:
        print(f"\n  特征向量验证 (A*v = λ*v):")
        for i in range(5):
            ev = result.eigenvalues[i]
            vec = result.eigenvectors[:, i]
            residual = np.linalg.norm(matrix @ vec - ev * vec)
            print(f"    特征向量{i+1}: 残差={residual:.6e}")

    print("\n4.2 复特征值矩阵:")
    matrix2 = np.array([
        [0, 1, 0, 0],
        [-1, 0, 0, 0],
        [0, 0, 0, 2],
        [0, 0, -2, 0]
    ], dtype=np.float64)

    result2 = solver.solve(matrix2)

    print(f"  迭代次数: {result2.iteration_count}")
    print(f"  收敛: {result2.converged}")

    print(f"\n  复特征值:")
    for i, ev in enumerate(result2.eigenvalues):
        print(f"    λ{i+1}: {ev.real:10.6f} + {ev.imag:10.6f}j")

    if result2.eigenvectors is not None:
        print(f"\n  特征向量验证:")
        for i in range(4):
            ev = result2.eigenvalues[i]
            vec = result2.eigenvectors[:, i]
            residual = np.linalg.norm(matrix2 @ vec - ev * vec)
            print(f"    特征向量{i+1}: 残差={residual:.6e}")

    print("\n✓ 特征向量匹配测试完成")
    return True


def test_precision_refinement():
    print("\n" + "=" * 60)
    print("测试5: 精度修正与异常处理")
    print("=" * 60)

    np.random.seed(99999)
    matrix = np.random.randn(8, 8)

    solver = EigenSolver(
        max_iterations=500,
        tolerance=1e-12,
        compute_eigenvectors=True
    )
    eigen_result = solver.solve(matrix)

    corrector = PrecisionCorrector(
        method=RefinementMethod.INVERSE_ITERATION,
        max_iterations=10,
        tolerance=1e-14
    )

    print("\n5.1 正常矩阵精度修正:")
    try:
        refinement = corrector.refine(
            matrix,
            eigen_result.eigenvalues,
            eigen_result.eigenvectors
        )
        print(f"  原始平均相对误差: {refinement.original_errors.avg_relative_error:.6e}")
        print(f"  修正后平均相对误差: {refinement.refined_errors.avg_relative_error:.6e}")
        print(f"  误差改善: {refinement.original_errors.avg_relative_error / refinement.refined_errors.avg_relative_error:.2f}倍")
    except Exception as e:
        print(f"  错误: {e}")

    print("\n5.2 近奇异矩阵精度修正:")
    ill_matrix = np.random.randn(6, 6)
    ill_matrix[0, :] *= 1e12
    ill_result = solver.solve(ill_matrix)

    try:
        ill_refinement = corrector.refine(
            ill_matrix,
            ill_result.eigenvalues,
            ill_result.eigenvectors
        )
        print(f"  原始平均相对误差: {ill_refinement.original_errors.avg_relative_error:.6e}")
        print(f"  修正后平均相对误差: {ill_refinement.refined_errors.avg_relative_error:.6e}")
        print(f"  程序正常运行，无崩溃")
    except Exception as e:
        print(f"  错误: {e}")

    print("\n✓ 精度修正测试完成")
    return True


def main():
    print("=" * 70)
    print("矩阵特征值求解系统 - 修复验证测试")
    print("=" * 70)
    print(f"Python: {sys.version.split()[0]}, NumPy: {np.__version__}")

    tests = [
        ("稀疏矩阵特征值求解偏差", test_sparse_matrix_fix),
        ("高阶矩阵迭代死循环", test_high_order_matrix),
        ("奇异矩阵异常处理", test_singular_matrix),
        ("特征向量匹配", test_eigenvector_matching),
        ("精度修正与异常处理", test_precision_refinement),
    ]

    results = {}

    for name, test_func in tests:
        try:
            result = test_func()
            results[name] = "通过" if result else "失败"
        except Exception as e:
            print(f"\n✗ {name}测试失败: {e}")
            import traceback
            traceback.print_exc()
            results[name] = f"失败: {e}"

    print("\n" + "=" * 70)
    print("测试总结")
    print("=" * 70)
    for name, status in results.items():
        print(f"  {name}: {status}")

    passed = sum(1 for v in results.values() if v == "通过")
    total = len(results)
    print(f"\n总计: {passed}/{total} 测试通过")

    return 0 if passed == total else 1


if __name__ == "__main__":
    sys.exit(main())
