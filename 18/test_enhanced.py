"""
增强功能测试脚本
测试: 奇异预警、批量求解、稳定性分析、高阶优化
"""

import numpy as np
from eigen_solver import EigenSolver, ShiftStrategy, SingularityWarning
from matrix_preprocessor import MatrixPreprocessor


def test_singularity_warning():
    print("\n" + "=" * 60)
    print("测试1: 矩阵奇异判定预警功能")
    print("=" * 60)

    print("\n1.1 正常矩阵:")
    matrix = np.random.randn(8, 8)
    solver = EigenSolver(max_iterations=200, enable_singularity_check=True)
    result = solver.solve(matrix)
    if result.singularity_info:
        print(f"  条件数: {result.singularity_info.condition_number:.2e}")
        print(f"  预警级别: {result.singularity_info.warning_level.value}")
        print(f"  预警消息: {result.singularity_info.warning_message}")
    print(f"  迭代次数: {result.iteration_count}")
    print(f"  收敛: {result.converged}")

    print("\n1.2 近奇异矩阵:")
    matrix = np.random.randn(8, 8)
    matrix[0, :] = matrix[1, :] + 1e-12 * np.random.randn(8)
    solver = EigenSolver(max_iterations=200, enable_singularity_check=True)
    result = solver.solve(matrix)
    if result.singularity_info:
        print(f"  条件数: {result.singularity_info.condition_number:.2e}")
        print(f"  预警级别: {result.singularity_info.warning_level.value}")
        print(f"  预警消息: {result.singularity_info.warning_message}")
    print(f"  迭代次数: {result.iteration_count}")
    print(f"  收敛: {result.converged}")

    print("\n1.3 奇异矩阵 (秩亏):")
    matrix = np.random.randn(8, 8)
    matrix[2, :] = matrix[0, :] + matrix[1, :]
    solver = EigenSolver(max_iterations=200, enable_singularity_check=True)
    result = solver.solve(matrix)
    if result.singularity_info:
        print(f"  条件数: {result.singularity_info.condition_number:.2e}")
        print(f"  秩: {result.singularity_info.rank}/{8}")
        print(f"  零空间维数: {result.singularity_info.null_space_dimension}")
        print(f"  预警级别: {result.singularity_info.warning_level.value}")
        print(f"  预警消息: {result.singularity_info.warning_message}")
    print(f"  迭代次数: {result.iteration_count}")
    print(f"  收敛: {result.converged}")

    print("\n1.4 高度病态矩阵:")
    matrix = np.eye(10)
    matrix[0, 0] = 1e15
    matrix[1, 1] = 1e-15
    solver = EigenSolver(max_iterations=200, enable_singularity_check=True)
    result = solver.solve(matrix)
    if result.singularity_info:
        print(f"  条件数: {result.singularity_info.condition_number:.2e}")
        print(f"  预警级别: {result.singularity_info.warning_level.value}")
        print(f"  预警消息: {result.singularity_info.warning_message}")
    print(f"  迭代次数: {result.iteration_count}")
    print(f"  收敛: {result.converged}")

    return True


def test_batch_solver():
    print("\n" + "=" * 60)
    print("测试2: 多阶矩阵批量求解功能")
    print("=" * 60)

    np.random.seed(12345)

    matrices = []
    sizes = [5, 8, 10, 12]
    for size in sizes:
        matrices.append(np.random.randn(size, size))

    print(f"\n2.1 串行批量求解 ({len(matrices)}个矩阵):")
    solver = EigenSolver(max_iterations=200, tolerance=1e-10,
                         compute_eigenvectors=False, enable_singularity_check=False)
    batch_result = solver.solve_batch(matrices, parallel=False)
    print(f"  总耗时: {batch_result.total_time:.4f}秒")
    print(f"  成功: {batch_result.successful_count}/{len(matrices)}")
    print(f"  失败: {batch_result.failed_count}")

    for i, (size, result) in enumerate(zip(sizes, batch_result.results)):
        ref = np.sort(np.linalg.eigvals(matrices[i]).real)[::-1]
        calc = np.sort(result.eigenvalues.real)[::-1]
        err = np.max(np.abs(calc - ref)) if len(calc) == len(ref) else float('inf')
        print(f"  矩阵{i+1} ({size}x{size}): 迭代{result.iteration_count}次, 误差={err:.2e}")

    print(f"\n2.2 并行批量求解 ({len(matrices)}个矩阵):")
    solver = EigenSolver(max_iterations=200, tolerance=1e-10,
                         compute_eigenvectors=False, enable_singularity_check=False)
    batch_result = solver.solve_batch(matrices, parallel=True, max_workers=4)
    print(f"  总耗时: {batch_result.total_time:.4f}秒")
    print(f"  成功: {batch_result.successful_count}/{len(matrices)}")
    print(f"  失败: {batch_result.failed_count}")

    for i, (size, result) in enumerate(zip(sizes, batch_result.results)):
        ref = np.sort(np.linalg.eigvals(matrices[i]).real)[::-1]
        calc = np.sort(result.eigenvalues.real)[::-1]
        err = np.max(np.abs(calc - ref)) if len(calc) == len(ref) else float('inf')
        print(f"  矩阵{i+1} ({size}x{size}): 迭代{result.iteration_count}次, 误差={err:.2e}")

    return True


def test_stability_analysis():
    print("\n" + "=" * 60)
    print("测试3: 特征值稳定性分析模块")
    print("=" * 60)

    np.random.seed(54321)

    print("\n3.1 良态矩阵稳定性分析:")
    matrix = np.eye(6) + 0.1 * np.random.randn(6, 6)
    solver = EigenSolver(max_iterations=200, tolerance=1e-10,
                         compute_eigenvectors=True, enable_stability_analysis=True)
    result = solver.solve(matrix)

    print(f"  迭代次数: {result.iteration_count}")
    print(f"  收敛: {result.converged}")
    if result.stability_info:
        print(f"  特征值稳定性分析:")
        stable_count = sum(1 for info in result.stability_info if info.stable)
        print(f"  稳定特征值: {stable_count}/{len(result.stability_info)}")
        for info in result.stability_info[:3]:
            print(f"    {info.details}")

    print("\n3.2 病态矩阵稳定性分析:")
    matrix = np.random.randn(6, 6)
    matrix = matrix @ np.diag([1e10, 1e8, 1e6, 1e4, 1e2, 1e0]) @ np.linalg.inv(matrix)
    solver = EigenSolver(max_iterations=200, tolerance=1e-10,
                         compute_eigenvectors=True, enable_stability_analysis=True)
    result = solver.solve(matrix)

    print(f"  迭代次数: {result.iteration_count}")
    print(f"  收敛: {result.converged}")
    if result.stability_info:
        print(f"  特征值稳定性分析:")
        stable_count = sum(1 for info in result.stability_info if info.stable)
        print(f"  稳定特征值: {stable_count}/{len(result.stability_info)}")
        for info in result.stability_info[:3]:
            print(f"    {info.details}")

    return True


def test_high_order_reliability():
    print("\n" + "=" * 60)
    print("测试4: 高阶矩阵计算可靠性")
    print("=" * 60)

    np.random.seed(99999)

    sizes = [30, 50, 80]
    for size in sizes:
        print(f"\n4.1 {size}x{size}矩阵:")
        matrix = np.random.randn(size, size)

        solver = EigenSolver(max_iterations=500, tolerance=1e-8,
                             compute_eigenvectors=False,
                             shift_strategy=ShiftStrategy.ADAPTIVE,
                             enable_singularity_check=False)
        result = solver.solve(matrix)

        try:
            ref = np.sort(np.linalg.eigvals(matrix).real)[::-1]
            calc = np.sort(result.eigenvalues.real)[::-1]
            err = np.max(np.abs(calc - ref)) if len(calc) == len(ref) else float('inf')
            print(f"  迭代次数: {result.iteration_count}")
            print(f"  收敛: {result.converged}")
            print(f"  最大误差: {err:.2e}")
        except Exception as e:
            print(f"  参考解计算失败: {e}")
            print(f"  迭代次数: {result.iteration_count}")
            print(f"  收敛: {result.converged}")

    print("\n4.2 高阶稀疏矩阵:")
    size = 50
    sparse_matrix = np.random.randn(size, size)
    for i in range(size):
        for j in range(size):
            if np.random.rand() > 0.2:
                sparse_matrix[i, j] = 0

    solver = EigenSolver(max_iterations=500, tolerance=1e-8,
                         compute_eigenvectors=False,
                         shift_strategy=ShiftStrategy.ADAPTIVE,
                         enable_singularity_check=False)
    result = solver.solve(sparse_matrix)

    try:
        ref = np.sort(np.linalg.eigvals(sparse_matrix).real)[::-1]
        calc = np.sort(result.eigenvalues.real)[::-1]
        err = np.max(np.abs(calc - ref)) if len(calc) == len(ref) else float('inf')
        print(f"  非零元素: {np.count_nonzero(sparse_matrix)}/{size*size}")
        print(f"  迭代次数: {result.iteration_count}")
        print(f"  收敛: {result.converged}")
        print(f"  最大误差: {err:.2e}")
    except Exception as e:
        print(f"  参考解计算失败: {e}")
        print(f"  迭代次数: {result.iteration_count}")
        print(f"  收敛: {result.converged}")

    return True


def test_combined_features():
    print("\n" + "=" * 60)
    print("测试5: 组合功能测试")
    print("=" * 60)

    np.random.seed(11111)

    print("\n5.1 奇异预警 + 稳定性分析:")
    matrix = np.random.randn(6, 6)
    matrix[0, :] = matrix[1, :] * 0.5 + 1e-10 * np.random.randn(6)

    solver = EigenSolver(max_iterations=200, tolerance=1e-10,
                         compute_eigenvectors=True,
                         enable_singularity_check=True,
                         enable_stability_analysis=True)
    result = solver.solve(matrix)

    print(f"  迭代次数: {result.iteration_count}")
    print(f"  收敛: {result.converged}")
    if result.singularity_info:
        print(f"  奇异预警: {result.singularity_info.warning_level.value}")
        print(f"  条件数: {result.singularity_info.condition_number:.2e}")
    if result.stability_info:
        stable_count = sum(1 for info in result.stability_info if info.stable)
        print(f"  稳定特征值: {stable_count}/{len(result.stability_info)}")

    print("\n5.2 自适应位移策略测试:")
    matrix = np.random.randn(15, 15)
    solver = EigenSolver(max_iterations=300, tolerance=1e-10,
                         compute_eigenvectors=False,
                         shift_strategy=ShiftStrategy.ADAPTIVE,
                         enable_singularity_check=False)
    result = solver.solve(matrix)

    ref = np.sort(np.linalg.eigvals(matrix).real)[::-1]
    calc = np.sort(result.eigenvalues.real)[::-1]
    err = np.max(np.abs(calc - ref)) if len(calc) == len(ref) else float('inf')

    print(f"  迭代次数: {result.iteration_count}")
    print(f"  收敛: {result.converged}")
    print(f"  最大误差: {err:.2e}")

    print("\n5.3 警告日志测试:")
    matrix = np.random.randn(8, 8)
    matrix[3, :] = matrix[0, :] + matrix[1, :]
    solver = EigenSolver(max_iterations=200, enable_singularity_check=True)
    result = solver.solve(matrix)

    warnings = solver.get_warning_log()
    print(f"  警告数量: {len(warnings)}")
    for w in warnings:
        print(f"    {w}")

    return True


def main():
    print("=" * 60)
    print("矩阵特征值求解系统 - 增强功能测试")
    print("=" * 60)

    tests = [
        ("奇异判定预警", test_singularity_warning),
        ("批量求解功能", test_batch_solver),
        ("稳定性分析", test_stability_analysis),
        ("高阶矩阵可靠性", test_high_order_reliability),
        ("组合功能测试", test_combined_features),
    ]

    results = []
    for name, test_func in tests:
        try:
            result = test_func()
            results.append((name, result))
        except Exception as e:
            print(f"\n测试 {name} 失败: {e}")
            import traceback
            traceback.print_exc()
            results.append((name, False))

    print("\n" + "=" * 60)
    print("测试结果汇总")
    print("=" * 60)
    for name, result in results:
        status = "通过" if result else "失败"
        print(f"  {name}: {status}")

    all_passed = all(r[1] for r in results)
    print(f"\n总体结果: {'全部通过' if all_passed else '存在失败'}")

    return all_passed


if __name__ == "__main__":
    main()
