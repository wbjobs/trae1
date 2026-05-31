"""
结果校验模块
验证特征值和特征向量的正确性
提供多种验证方法和统计分析
"""

import numpy as np
from dataclasses import dataclass, field
from typing import List, Dict, Tuple, Optional
from enum import Enum


class ValidationMethod(Enum):
    RESIDUAL = "residual"
    CHARACTERISTIC_POLYNOMIAL = "characteristic_polynomial"
    TRACE_DETERMINANT = "trace_determinant"
    ORTHOGONALITY = "orthogonality"
    SPECTRAL_DECOMPOSITION = "spectral_decomposition"


@dataclass
class ValidationResult:
    is_valid: bool
    validation_methods: Dict[str, bool]
    error_details: Dict[str, float]
    eigenvalue_errors: np.ndarray
    eigenvector_errors: np.ndarray
    residual_norms: np.ndarray
    trace_error: float
    determinant_error: float
    orthogonality_error: float
    spectral_decomposition_error: float


class ResultValidator:
    def __init__(self, tolerance: float = 1e-10):
        self.tolerance = tolerance
        self.processing_log: List[str] = []

    def validate(self, matrix: np.ndarray, eigenvalues: np.ndarray,
                 eigenvectors: np.ndarray) -> ValidationResult:
        validation_methods = {}
        error_details = {}

        residual_valid, residual_norms = self._check_residual(matrix, eigenvalues, eigenvectors)
        validation_methods['residual'] = residual_valid
        error_details['max_residual'] = np.max(residual_norms)

        trace_valid, trace_error = self._check_trace(matrix, eigenvalues)
        validation_methods['trace'] = trace_valid
        error_details['trace_error'] = trace_error

        det_valid, det_error = self._check_determinant(matrix, eigenvalues)
        validation_methods['determinant'] = det_valid
        error_details['determinant_error'] = det_error

        ortho_valid, ortho_error = self._check_orthogonality(eigenvectors)
        validation_methods['orthogonality'] = ortho_valid
        error_details['orthogonality_error'] = ortho_error

        spectral_valid, spectral_error = self._check_spectral_decomposition(
            matrix, eigenvalues, eigenvectors
        )
        validation_methods['spectral_decomposition'] = spectral_valid
        error_details['spectral_decomposition_error'] = spectral_error

        eigenvalue_errors = np.array([
            self._compute_eigenvalue_error(matrix, eigenvalues[i], eigenvectors[:, i])
            for i in range(len(eigenvalues))
        ])

        eigenvector_errors = self._compute_eigenvector_errors(matrix, eigenvalues, eigenvectors)

        is_valid = all(validation_methods.values())

        self.processing_log.append(f"验证完成: {'通过' if is_valid else '未通过'}, "
                                    f"验证方法: {sum(validation_methods.values())}/{len(validation_methods)} 通过")

        return ValidationResult(
            is_valid=is_valid,
            validation_methods=validation_methods,
            error_details=error_details,
            eigenvalue_errors=eigenvalue_errors,
            eigenvector_errors=eigenvector_errors,
            residual_norms=residual_norms,
            trace_error=trace_error,
            determinant_error=det_error,
            orthogonality_error=ortho_error,
            spectral_decomposition_error=spectral_error
        )

    def _check_residual(self, matrix: np.ndarray, eigenvalues: np.ndarray,
                        eigenvectors: np.ndarray) -> Tuple[bool, np.ndarray]:
        n = matrix.shape[0]
        residuals = np.zeros(n)

        for i in range(n):
            ev = eigenvalues[i]
            vec = eigenvectors[:, i]
            residual = matrix @ vec - ev * vec
            residuals[i] = np.linalg.norm(residual)

        max_residual = np.max(residuals)
        is_valid = max_residual < self.tolerance * (np.linalg.norm(matrix) + 1.0)

        return is_valid, residuals

    def _check_trace(self, matrix: np.ndarray, eigenvalues: np.ndarray) -> Tuple[bool, float]:
        trace_matrix = np.trace(matrix)
        trace_eigenvalues = np.sum(eigenvalues).real

        error = abs(trace_matrix - trace_eigenvalues)
        is_valid = error < self.tolerance * (abs(trace_matrix) + 1.0)

        return is_valid, error

    def _check_determinant(self, matrix: np.ndarray, eigenvalues: np.ndarray) -> Tuple[bool, float]:
        try:
            det_matrix = np.linalg.det(matrix)
        except np.linalg.LinAlgError:
            det_matrix = np.prod(np.linalg.eigvals(matrix)).real

        det_eigenvalues = np.prod(eigenvalues).real

        error = abs(det_matrix - det_eigenvalues)
        is_valid = error < self.tolerance * (abs(det_matrix) + 1.0)

        return is_valid, error

    def _check_orthogonality(self, eigenvectors: np.ndarray) -> Tuple[bool, float]:
        n = eigenvectors.shape[1]
        gram_matrix = eigenvectors.T @ eigenvectors
        identity = np.eye(n)

        error = np.linalg.norm(gram_matrix - identity)
        is_valid = error < self.tolerance * n

        return is_valid, error

    def _check_spectral_decomposition(self, matrix: np.ndarray, eigenvalues: np.ndarray,
                                      eigenvectors: np.ndarray) -> Tuple[bool, float]:
        n = matrix.shape[0]

        try:
            V_inv = np.linalg.inv(eigenvectors)
            reconstructed = eigenvectors @ np.diag(eigenvalues) @ V_inv
        except np.linalg.LinAlgError:
            return False, float('inf')

        error = np.linalg.norm(matrix - reconstructed)
        is_valid = error < self.tolerance * np.linalg.norm(matrix)

        return is_valid, error

    def _compute_eigenvalue_error(self, matrix: np.ndarray, eigenvalue: complex,
                                  eigenvector: np.ndarray) -> float:
        residual = matrix @ eigenvector - eigenvalue * eigenvector
        vec_norm = np.linalg.norm(eigenvector)

        if vec_norm < 1e-16:
            return float('inf')

        return np.linalg.norm(residual) / vec_norm

    def _compute_eigenvector_errors(self, matrix: np.ndarray, eigenvalues: np.ndarray,
                                     eigenvectors: np.ndarray) -> np.ndarray:
        n = matrix.shape[0]
        errors = np.zeros(n)

        for i in range(n):
            ev = eigenvalues[i]
            vec = eigenvectors[:, i]

            try:
                lam = np.vdot(vec, matrix @ vec) / np.vdot(vec, vec)
                A_shifted = matrix - lam * np.eye(n)
                A_inv = np.linalg.inv(A_shifted)

                error = np.linalg.norm(A_inv @ vec) / np.linalg.norm(vec)
                errors[i] = error
            except np.linalg.LinAlgError:
                errors[i] = float('inf')

        return errors

    def compare_with_reference(self, computed_eigenvalues: np.ndarray,
                                reference_eigenvalues: np.ndarray) -> Dict[str, float]:
        sorted_computed = np.sort(np.abs(computed_eigenvalues))
        sorted_reference = np.sort(np.abs(reference_eigenvalues))

        if len(sorted_computed) != len(sorted_reference):
            return {'error': '维度不匹配'}

        abs_errors = np.abs(sorted_computed - sorted_reference)
        rel_errors = abs_errors / (np.abs(sorted_reference) + 1e-16)

        return {
            'max_absolute_error': np.max(abs_errors),
            'mean_absolute_error': np.mean(abs_errors),
            'max_relative_error': np.max(rel_errors),
            'mean_relative_error': np.mean(rel_errors),
            'mse': np.mean(abs_errors ** 2),
            'rmse': np.sqrt(np.mean(abs_errors ** 2))
        }

    def check_spectrum_bounds(self, eigenvalues: np.ndarray,
                              matrix: np.ndarray) -> Dict[str, bool]:
        result = {}

        row_sums = np.sum(np.abs(matrix), axis=1)
        col_sums = np.sum(np.abs(matrix), axis=0)

        max_row_sum = np.max(row_sums)
        max_col_sum = np.max(col_sums)

        result['gershgorin_circle'] = all(
            abs(ev) <= max_row_sum for ev in eigenvalues
        )

        try:
            norm_1 = np.linalg.norm(matrix, ord=1)
            norm_inf = np.linalg.norm(matrix, ord=np.inf)
            max_bound = max(norm_1, norm_inf)

            result['norm_bound'] = all(
                abs(ev) <= max_bound for ev in eigenvalues
            )
        except np.linalg.LinAlgError:
            result['norm_bound'] = False

        return result

    def generate_validation_report(self, result: ValidationResult) -> str:
        report_lines = [
            "=" * 60,
            "特征值求解验证报告",
            "=" * 60,
            f"整体验证结果: {'通过' if result.is_valid else '未通过'}",
            "",
            "各验证方法结果:",
        ]

        for method, passed in result.validation_methods.items():
            status = "通过" if passed else "未通过"
            report_lines.append(f"  {method}: {status}")

        report_lines.extend([
            "",
            "误差详情:",
        ])

        for key, value in result.error_details.items():
            report_lines.append(f"  {key}: {value:.6e}")

        report_lines.extend([
            "",
            f"最大特征值误差: {np.max(result.eigenvalue_errors):.6e}",
            f"最大特征向量误差: {np.max(result.eigenvector_errors):.6e}",
            "=" * 60
        ])

        return "\n".join(report_lines)

    def get_processing_log(self) -> List[str]:
        return self.processing_log.copy()

    def clear_log(self):
        self.processing_log.clear()
