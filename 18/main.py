"""
矩阵特征值与特征向量高精度求解系统
主程序入口

功能模块:
1. 矩阵预处理 - 稀疏矩阵支持、矩阵平衡、Hessenberg变换
2. QR分解核心 - Householder变换、Givens旋转
3. 特征迭代求解 - 带位移QR算法
4. 精度修正 - 逆迭代法、Newton法、Rayleigh商迭代
5. 结果校验 - 残差验证、迹/行列式验证、正交性验证
6. 矩阵可视化 - 矩阵结构、特征值分布、迭代过程
"""

import numpy as np
from typing import Dict, Optional, List, Tuple
from dataclasses import dataclass
import json
import time
import sys
import os

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

from sparse_compat import (issparse, from_dense, sparse_random, HAS_SCIPY)
if HAS_SCIPY:
    from scipy import sparse
from matrix_preprocessor import MatrixPreprocessor, MatrixInfo, MatrixType
from qr_decomposition import QRDecomposition, QRResult, QRMethod
from eigen_solver import (EigenSolver, EigenResult, ShiftStrategy,
                            ConvergenceCriterion, IterationData)
from precision_corrector import (PrecisionCorrector, RefinementResult,
                                   ErrorReport, RefinementMethod)
from result_validator import ResultValidator, ValidationResult, ValidationMethod
from matrix_visualizer import MatrixVisualizer, VisualizationConfig


@dataclass
class SystemConfig:
    max_iterations: int = 1000
    tolerance: float = 1e-14
    shift_strategy: ShiftStrategy = ShiftStrategy.WILKINSON
    qr_method: QRMethod = QRMethod.HOUSEHOLDER
    refinement_method: RefinementMethod = RefinementMethod.INVERSE_ITERATION
    compute_eigenvectors: bool = True
    enable_refinement: bool = True
    enable_validation: bool = True
    enable_visualization: bool = True
    output_dir: str = "./output"


class EigenSystem:
    def __init__(self, config: Optional[SystemConfig] = None):
        self.config = config or SystemConfig()

        self.preprocessor = MatrixPreprocessor(tolerance=self.config.tolerance)
        self.qr_solver = QRDecomposition(
            method=self.config.qr_method,
            tolerance=self.config.tolerance
        )
        self.eigen_solver = EigenSolver(
            max_iterations=self.config.max_iterations,
            tolerance=self.config.tolerance,
            shift_strategy=self.config.shift_strategy,
            compute_eigenvectors=self.config.compute_eigenvectors
        )
        self.precision_corrector = PrecisionCorrector(
            method=self.config.refinement_method,
            tolerance=self.config.tolerance
        )
        self.validator = ResultValidator(tolerance=1e-10)
        self.visualizer = MatrixVisualizer()

        self.results: Dict = {}
        self.processing_log: List[str] = []

    def solve(self, matrix) -> Dict:
        import os
        start_time = time.time()

        if issparse(matrix):
            matrix = matrix.toarray()
        matrix = np.array(matrix, dtype=np.float64)

        self._log(f"开始处理 {matrix.shape[0]}x{matrix.shape[1]} 矩阵")

        matrix_info = self.preprocessor.analyze_matrix(matrix)
        self._log(f"矩阵类型: {matrix_info.matrix_type.value}, "
                  f"稀疏度: {matrix_info.density:.4f}")

        eigen_result = self.eigen_solver.solve(matrix)
        self.results['eigenvalues'] = eigen_result.eigenvalues
        self.results['eigenvectors'] = eigen_result.eigenvectors
        self.results['converged'] = eigen_result.converged
        self.results['iterations'] = eigen_result.iteration_count
        self.results['computation_time'] = eigen_result.computation_time

        self._log(f"特征值求解完成: {eigen_result.iteration_count}次迭代, "
                  f"耗时={eigen_result.computation_time:.4f}秒")

        refinement_result = None
        if self.config.enable_refinement and self.config.compute_eigenvectors:
            self._log("开始精度修正...")
            refinement_result = self.precision_corrector.refine(
                matrix,
                eigen_result.eigenvalues,
                eigen_result.eigenvectors
            )
            self.results['refined_eigenvalues'] = refinement_result.eigenvalues_refined
            self.results['refined_eigenvectors'] = refinement_result.eigenvectors_refined
            self._log(f"精度修正完成, 平均相对误差: "
                      f"{refinement_result.refined_errors.avg_relative_error:.6e}")

        validation_result = None
        if self.config.enable_validation:
            self._log("开始结果校验...")
            if refinement_result:
                validation_result = self.validator.validate(
                    matrix,
                    refinement_result.eigenvalues_refined,
                    refinement_result.eigenvectors_refined
                )
            else:
                validation_result = self.validator.validate(
                    matrix,
                    eigen_result.eigenvalues,
                    eigen_result.eigenvectors
                )
            self.results['validation'] = {
                'is_valid': validation_result.is_valid,
                'methods': validation_result.validation_methods,
                'error_details': validation_result.error_details
            }
            self._log(f"校验结果: {'通过' if validation_result.is_valid else '未通过'}")

        total_time = time.time() - start_time
        self.results['total_time'] = total_time

        if self.config.enable_visualization:
            self._log("生成可视化图表...")
            os.makedirs(self.config.output_dir, exist_ok=True)
            self._generate_visualizations(matrix, eigen_result, refinement_result,
                                           validation_result)

        self._log(f"处理完成, 总耗时: {total_time:.4f}秒")

        return self.results

    def _generate_visualizations(self, matrix, eigen_result, refinement_result,
                                  validation_result):
        import os

        self.visualizer.visualize_matrix(
            matrix,
            title="输入矩阵结构",
            save_path=os.path.join(self.config.output_dir, "matrix_structure.png")
        )

        eigenvalues = self.results.get('refined_eigenvalues', eigen_result.eigenvalues)
        self.visualizer.visualize_eigenvalues(
            eigenvalues,
            title="特征值分布",
            matrix=matrix,
            save_path=os.path.join(self.config.output_dir, "eigenvalue_distribution.png")
        )

        eigenvectors = self.results.get('refined_eigenvectors', eigen_result.eigenvectors)
        if eigenvectors is not None:
            self.visualizer.visualize_eigenvectors(
                eigenvectors,
                eigenvalues=eigenvalues,
                top_k=3,
                save_path=os.path.join(self.config.output_dir, "eigenvectors.png")
            )

        self.visualizer.visualize_iteration_history(
            eigen_result.iteration_history,
            title="QR算法迭代收敛过程",
            save_path=os.path.join(self.config.output_dir, "iteration_history.png")
        )

        if refinement_result:
            errors = {
                '原始特征值误差': refinement_result.original_errors.eigenvalue_errors,
                '修正后特征值误差': refinement_result.refined_errors.eigenvalue_errors,
                '原始残差': refinement_result.original_errors.residual_norms,
                '修正后残差': refinement_result.refined_errors.residual_norms,
            }
            self.visualizer.visualize_error_analysis(
                errors,
                title="精度修正误差对比",
                save_path=os.path.join(self.config.output_dir, "error_analysis.png")
            )

        self.visualizer.create_summary_dashboard(
            matrix,
            eigenvalues,
            eigenvectors=eigenvectors,
            iteration_data=eigen_result.iteration_history,
            errors={
                '特征值误差': self.results.get('validation', {}).get('error_details', {}).get('max_residual', np.zeros(matrix.shape[0])),
            } if validation_result else None,
            save_path=os.path.join(self.config.output_dir, "summary_dashboard.png")
        )

    def print_results(self, results: Optional[Dict] = None):
        results = results or self.results

        print("\n" + "=" * 70)
        print("矩阵特征值与特征向量高精度求解系统 - 结果报告")
        print("=" * 70)

        if 'eigenvalues' in results:
            eigenvalues = results['eigenvalues']
            print(f"\n【特征值】(共{len(eigenvalues)}个)")
            print("-" * 50)

            for i, ev in enumerate(eigenvalues):
                if abs(ev.imag) < 1e-10:
                    print(f"  λ{i+1:3d} = {ev.real:18.12f}")
                else:
                    print(f"  λ{i+1:3d} = {ev.real:12.6f} + {ev.imag:12.6f}j")

        if 'iterations' in results:
            print(f"\n【求解信息】")
            print(f"  迭代次数: {results['iterations']}")
            print(f"  收敛状态: {'是' if results.get('converged', False) else '否'}")
            print(f"  计算耗时: {results.get('computation_time', 0):.4f}秒")
            print(f"  总耗时: {results.get('total_time', 0):.4f}秒")

        if 'validation' in results:
            val = results['validation']
            print(f"\n【验证结果】")
            print(f"  整体验证: {'通过' if val['is_valid'] else '未通过'}")
            for method, passed in val['methods'].items():
                print(f"    {method}: {'通过' if passed else '未通过'}")

        print("\n" + "=" * 70)

    def export_iteration_data(self, eigen_result: EigenResult,
                               filepath: str):
        data = []
        for item in eigen_result.iteration_history:
            entry = {
                'iteration': item.iteration,
                'shift': item.shift,
                'max_off_diagonal': item.max_off_diagonal,
                'residual_norm': item.residual_norm,
            }
            if item.eigenvalue_estimate is not None:
                entry['eigenvalue_estimates'] = item.eigenvalue_estimate.tolist()
            data.append(entry)

        with open(filepath, 'w', encoding='utf-8') as f:
            json.dump(data, f, indent=2, ensure_ascii=False)

        self._log(f"迭代数据已导出: {filepath}")

    def export_error_report(self, refinement_result: RefinementResult,
                            filepath: str):
        report = {
            'original': {
                'eigenvalue_errors': refinement_result.original_errors.eigenvalue_errors.tolist(),
                'residual_norms': refinement_result.original_errors.residual_norms.tolist(),
                'relative_errors': refinement_result.original_errors.relative_errors.tolist(),
                'max_eigenvalue_error': refinement_result.original_errors.max_eigenvalue_error,
                'max_eigenvector_error': refinement_result.original_errors.max_eigenvector_error,
                'avg_relative_error': refinement_result.original_errors.avg_relative_error,
            },
            'refined': {
                'eigenvalue_errors': refinement_result.refined_errors.eigenvalue_errors.tolist(),
                'residual_norms': refinement_result.refined_errors.residual_norms.tolist(),
                'relative_errors': refinement_result.refined_errors.relative_errors.tolist(),
                'max_eigenvalue_error': refinement_result.refined_errors.max_eigenvalue_error,
                'max_eigenvector_error': refinement_result.refined_errors.max_eigenvector_error,
                'avg_relative_error': refinement_result.refined_errors.avg_relative_error,
            },
            'method': refinement_result.method.value,
            'refinement_iterations': refinement_result.refinement_iterations,
        }

        with open(filepath, 'w', encoding='utf-8') as f:
            json.dump(report, f, indent=2, ensure_ascii=False)

        self._log(f"误差报表已导出: {filepath}")

    def _log(self, message: str):
        self.processing_log.append(message)
        print(f"[INFO] {message}")

    def get_log(self) -> List[str]:
        return self.processing_log.copy()


def create_test_matrix(matrix_type: str = "random", size: int = 10,
                       seed: Optional[int] = None) -> np.ndarray:
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
        density = 0.1
        A = sparse_random((size, size), density=density, format='csr')
        return A.toarray()

    elif matrix_type == "ill_conditioned":
        A = np.random.randn(size, size)
        A[0, :] *= 1e6
        return A

    elif matrix_type == "known_eigenvalues":
        eigenvalues = np.array([1.0, 2.0, 3.0, 5.0, 8.0, 13.0, 21.0, 34.0, 55.0, 89.0][:size])
        D = np.diag(eigenvalues)
        Q = np.linalg.qr(np.random.randn(size, size))[0]
        return Q @ D @ Q.T

    else:
        return np.random.randn(size, size)


def main():
    print("=" * 70)
    print("矩阵特征值与特征向量高精度求解系统")
    print("=" * 70)
    print()

    config = SystemConfig(
        max_iterations=500,
        tolerance=1e-12,
        shift_strategy=ShiftStrategy.WILKINSON,
        qr_method=QRMethod.HOUSEHOLDER,
        refinement_method=RefinementMethod.INVERSE_ITERATION,
        enable_refinement=True,
        enable_validation=True,
        enable_visualization=True,
        output_dir="./output"
    )

    system = EigenSystem(config)

    test_cases = [
        ("对称矩阵", "symmetric", 8, 42),
        ("三对角矩阵", "tridiagonal", 10, 123),
        ("上Hessenberg矩阵", "upper_hessenberg", 8, 456),
        ("已知特征值矩阵", "known_eigenvalues", 6, 789),
        ("随机矩阵", "random", 5, 2024),
    ]

    all_results = {}

    for name, mtype, size, seed in test_cases:
        print(f"\n{'='*70}")
        print(f"测试用例: {name} ({size}x{size})")
        print(f"{'='*70}")

        matrix = create_test_matrix(mtype, size, seed)

        print(f"矩阵条件数: {np.linalg.cond(matrix):.2e}")
        print(f"非零元素数: {np.count_nonzero(matrix)}")

        results = system.solve(matrix)
        all_results[name] = results

        system.print_results(results)

        import os
        os.makedirs(config.output_dir, exist_ok=True)

        eigen_result = system.eigen_solver.solve(matrix)
        system.export_iteration_data(
            eigen_result,
            os.path.join(config.output_dir, f"iteration_data_{name}.json")
        )

    print("\n" + "=" * 70)
    print("所有测试用例完成")
    print("=" * 70)

    print("\n【系统日志】")
    for log in system.get_log():
        print(f"  {log}")

    return all_results


if __name__ == "__main__":
    main()
