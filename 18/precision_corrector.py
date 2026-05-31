"""
精度修正与误差分析模块 - 修复版
提供特征值和特征向量的精度修正和误差分析功能
修复: 奇异矩阵处理、异常捕获、数值稳定性
"""

import numpy as np
from dataclasses import dataclass, field
from typing import List, Tuple, Optional, Dict
from enum import Enum


class RefinementMethod(Enum):
    INVERSE_ITERATION = "inverse_iteration"
    NEWTON = "newton"
    RAYLEIGH_QUOTIENT = "rayleigh_quotient"


@dataclass
class ErrorReport:
    eigenvalue_errors: np.ndarray
    eigenvector_errors: np.ndarray
    residual_norms: np.ndarray
    condition_numbers: np.ndarray
    forward_errors: np.ndarray
    backward_errors: np.ndarray
    relative_errors: np.ndarray
    max_eigenvalue_error: float
    max_eigenvector_error: float
    avg_relative_error: float


@dataclass
class RefinementResult:
    eigenvalues_refined: np.ndarray
    eigenvectors_refined: np.ndarray
    original_errors: ErrorReport
    refined_errors: ErrorReport
    refinement_iterations: int
    method: RefinementMethod
    converged: bool


class PrecisionCorrector:
    def __init__(self,
                 method: RefinementMethod = RefinementMethod.INVERSE_ITERATION,
                 max_iterations: int = 10,
                 tolerance: float = 1e-14):
        self.method = method
        self.max_iterations = max_iterations
        self.tolerance = tolerance
        self.processing_log: List[str] = []

    def refine(self, matrix: np.ndarray, eigenvalues: np.ndarray,
               eigenvectors: np.ndarray) -> RefinementResult:
        n = matrix.shape[0]

        try:
            original_errors = self.compute_error_report(matrix, eigenvalues, eigenvectors)
        except Exception as e:
            self.processing_log.append(f"原始误差计算失败: {e}")
            original_errors = ErrorReport(
                eigenvalue_errors=np.zeros(n),
                eigenvector_errors=np.zeros(n),
                residual_norms=np.full(n, np.inf),
                condition_numbers=np.ones(n),
                forward_errors=np.full(n, np.inf),
                backward_errors=np.full(n, np.inf),
                relative_errors=np.full(n, np.inf),
                max_eigenvalue_error=np.inf,
                max_eigenvector_error=np.inf,
                avg_relative_error=np.inf
            )

        ev_refined = eigenvalues.copy()
        vec_refined = eigenvectors.copy()

        total_iterations = 0
        converged = True

        for i in range(n):
            try:
                ev = eigenvalues[i]
                vec = eigenvectors[:, i].copy()

                if np.linalg.norm(vec) < 1e-16:
                    continue

                if self.method == RefinementMethod.INVERSE_ITERATION:
                    ev_new, vec_new, iters = self._inverse_iteration_refinement(matrix, ev, vec)
                elif self.method == RefinementMethod.NEWTON:
                    ev_new, vec_new, iters = self._newton_refinement(matrix, ev, vec)
                else:
                    ev_new, vec_new, iters = self._rayleigh_quotient_refinement(matrix, ev, vec)

                ev_refined[i] = ev_new
                vec_refined[:, i] = vec_new
                total_iterations += iters
            except Exception as e:
                self.processing_log.append(f"特征值{i+1}修正失败: {e}")
                converged = False

        try:
            refined_errors = self.compute_error_report(matrix, ev_refined, vec_refined)
        except Exception as e:
            self.processing_log.append(f"修正后误差计算失败: {e}")
            refined_errors = original_errors

        self.processing_log.append(f"精度修正完成: 方法={self.method.value}, "
                                    f"平均迭代次数={total_iterations/max(n, 1):.1f}")

        return RefinementResult(
            eigenvalues_refined=ev_refined,
            eigenvectors_refined=vec_refined,
            original_errors=original_errors,
            refined_errors=refined_errors,
            refinement_iterations=total_iterations,
            method=self.method,
            converged=converged
        )

    def compute_error_report(self, matrix: np.ndarray, eigenvalues: np.ndarray,
                             eigenvectors: np.ndarray) -> ErrorReport:
        n = matrix.shape[0]

        residual_norms = np.zeros(n)
        for i in range(n):
            try:
                ev = eigenvalues[i]
                vec = eigenvectors[:, i]
                if np.linalg.norm(vec) > 1e-16:
                    residual = matrix @ vec - ev * vec
                    residual_norms[i] = np.linalg.norm(residual)
                else:
                    residual_norms[i] = np.inf
            except Exception:
                residual_norms[i] = np.inf

        vec_norms = np.array([np.linalg.norm(eigenvectors[:, i]) if np.linalg.norm(eigenvectors[:, i]) > 1e-16 else np.inf
                              for i in range(n)])
        relative_errors = residual_norms / np.where(vec_norms != np.inf, vec_norms, 1.0)

        try:
            matrix_norm = np.linalg.norm(matrix)
        except Exception:
            matrix_norm = 1.0
        backward_errors = residual_norms / (matrix_norm * np.where(vec_norms != np.inf, vec_norms, 1.0) + 1e-16)

        try:
            condition_numbers = self._estimate_eigenvalue_condition_numbers(matrix, eigenvectors)
        except Exception:
            condition_numbers = np.ones(n)

        forward_errors = backward_errors * condition_numbers

        eigenvalue_errors = forward_errors.copy()
        eigenvector_errors = relative_errors.copy()

        valid_mask = np.isfinite(eigenvalue_errors)
        if np.any(valid_mask):
            max_ev_error = np.max(np.abs(eigenvalue_errors[valid_mask]))
        else:
            max_ev_error = np.inf

        valid_mask_vec = np.isfinite(eigenvector_errors)
        if np.any(valid_mask_vec):
            max_vec_error = np.max(np.abs(eigenvector_errors[valid_mask_vec]))
        else:
            max_vec_error = np.inf

        valid_mask_rel = np.isfinite(relative_errors)
        if np.any(valid_mask_rel):
            avg_rel_error = np.mean(relative_errors[valid_mask_rel])
        else:
            avg_rel_error = np.inf

        return ErrorReport(
            eigenvalue_errors=eigenvalue_errors,
            eigenvector_errors=eigenvector_errors,
            residual_norms=residual_norms,
            condition_numbers=condition_numbers,
            forward_errors=forward_errors,
            backward_errors=backward_errors,
            relative_errors=relative_errors,
            max_eigenvalue_error=max_ev_error,
            max_eigenvector_error=max_vec_error,
            avg_relative_error=avg_rel_error
        )

    def _inverse_iteration_refinement(self, matrix: np.ndarray, eigenvalue: complex,
                                       eigenvector: np.ndarray
                                       ) -> Tuple[complex, np.ndarray, int]:
        n = matrix.shape[0]
        A = matrix.copy().astype(complex)
        A[np.diag_indices(n)] -= eigenvalue

        shift = 0.0
        A_inv = None
        for attempt in range(10):
            try:
                A_shifted = A + shift * np.eye(n)
                try:
                    cond = np.linalg.cond(A_shifted)
                    if cond > 1e15:
                        shift += 1e-8 * (10 ** attempt)
                        continue
                except Exception:
                    pass
                A_inv = np.linalg.inv(A_shifted)
                break
            except np.linalg.LinAlgError:
                shift += 1e-6 * (10 ** attempt)
                A_inv = None

        if A_inv is None:
            try:
                rayleigh = np.vdot(eigenvector, matrix @ eigenvector) / np.vdot(eigenvector, eigenvector)
            except Exception:
                rayleigh = eigenvalue
            return rayleigh, eigenvector, 0

        vec = eigenvector.copy().astype(complex)
        if np.linalg.norm(vec) > 1e-16:
            vec /= np.linalg.norm(vec)

        for iteration in range(self.max_iterations):
            try:
                w = A_inv @ vec
            except Exception:
                break

            norm_w = np.linalg.norm(w)
            if norm_w < 1e-16:
                break

            vec_new = w / norm_w

            try:
                rayleigh = np.vdot(vec_new, matrix @ vec_new) / np.vdot(vec_new, vec_new)
                residual = matrix @ vec_new - rayleigh * vec_new
                residual_norm = np.linalg.norm(residual)

                if residual_norm < self.tolerance:
                    return rayleigh, vec_new, iteration + 1
            except Exception:
                break

            vec = vec_new

        try:
            rayleigh = np.vdot(vec, matrix @ vec) / np.vdot(vec, vec)
        except Exception:
            rayleigh = eigenvalue
        return rayleigh, vec, self.max_iterations

    def _newton_refinement(self, matrix: np.ndarray, eigenvalue: complex,
                           eigenvector: np.ndarray
                           ) -> Tuple[complex, np.ndarray, int]:
        n = matrix.shape[0]
        ev = eigenvalue
        vec = eigenvector.copy().astype(complex)
        if np.linalg.norm(vec) > 1e-16:
            vec /= np.linalg.norm(vec)

        for iteration in range(self.max_iterations):
            try:
                residual = matrix @ vec - ev * vec
                residual_norm = np.linalg.norm(residual)
            except Exception:
                break

            if residual_norm < self.tolerance:
                try:
                    rayleigh = np.vdot(vec, matrix @ vec) / np.vdot(vec, vec)
                except Exception:
                    rayleigh = ev
                return rayleigh, vec, iteration + 1

            try:
                J = np.zeros((n + 1, n + 1), dtype=complex)
                J[:n, :n] = matrix - ev * np.eye(n)
                J[:n, n] = -vec
                J[n, :n] = 2 * np.conj(vec).T
                J[n, n] = 0

                rhs = np.zeros(n + 1, dtype=complex)
                rhs[:n] = -residual
                rhs[n] = 1 - np.vdot(vec, vec)

                try:
                    delta = np.linalg.solve(J, rhs)
                except np.linalg.LinAlgError:
                    try:
                        delta = np.linalg.lstsq(J, rhs, rcond=None)[0]
                    except Exception:
                        break
            except Exception:
                break

            vec_new = vec + delta[:n]
            ev_new = ev + delta[n]

            norm_vec_new = np.linalg.norm(vec_new)
            if norm_vec_new > 1e-16:
                vec_new /= norm_vec_new

            vec = vec_new
            ev = ev_new

        return ev, vec, self.max_iterations

    def _rayleigh_quotient_refinement(self, matrix: np.ndarray, eigenvalue: complex,
                                       eigenvector: np.ndarray
                                       ) -> Tuple[complex, np.ndarray, int]:
        n = matrix.shape[0]
        vec = eigenvector.copy().astype(complex)
        if np.linalg.norm(vec) > 1e-16:
            vec /= np.linalg.norm(vec)

        for iteration in range(self.max_iterations):
            try:
                ev = np.vdot(vec, matrix @ vec) / np.vdot(vec, vec)

                residual = matrix @ vec - ev * vec
                residual_norm = np.linalg.norm(residual)
            except Exception:
                break

            if residual_norm < self.tolerance:
                return ev, vec, iteration + 1

            try:
                A = matrix - ev * np.eye(n)
                try:
                    delta = np.linalg.solve(A, -residual)
                except np.linalg.LinAlgError:
                    try:
                        delta = np.linalg.lstsq(A, -residual, rcond=None)[0]
                    except Exception:
                        break
            except Exception:
                break

            vec_new = vec + delta
            norm_vec_new = np.linalg.norm(vec_new)

            if norm_vec_new < 1e-16:
                break

            vec = vec_new / norm_vec_new

        try:
            ev = np.vdot(vec, matrix @ vec) / np.vdot(vec, vec)
        except Exception:
            ev = eigenvalue
        return ev, vec, self.max_iterations

    def _estimate_eigenvalue_condition_numbers(self, matrix: np.ndarray,
                                               eigenvectors: np.ndarray
                                               ) -> np.ndarray:
        n = matrix.shape[0]
        condition_numbers = np.ones(n)

        for i in range(n):
            try:
                vec = eigenvectors[:, i]
                if np.linalg.norm(vec) < 1e-16:
                    continue
                left_vec = self._compute_left_eigenvector(matrix, vec)

                if left_vec is not None:
                    dot_product = abs(np.vdot(left_vec, vec))
                    if dot_product > 1e-16:
                        condition_numbers[i] = 1.0 / dot_product
                    else:
                        condition_numbers[i] = 1e16
            except Exception:
                condition_numbers[i] = 1.0

        return condition_numbers

    def _compute_left_eigenvector(self, matrix: np.ndarray,
                                   right_eigenvector: np.ndarray
                                   ) -> Optional[np.ndarray]:
        try:
            ev = np.vdot(right_eigenvector, matrix @ right_eigenvector) / np.vdot(right_eigenvector, right_eigenvector)
            A = (matrix - ev * np.eye(matrix.shape[0])).T
            try:
                left_vec = np.linalg.lstsq(A, np.zeros(matrix.shape[0]), rcond=None)[0]
            except Exception:
                return None
            norm = np.linalg.norm(left_vec)
            if norm > 1e-16:
                return left_vec / norm
            return None
        except Exception:
            return None

    def generate_error_summary(self, report: ErrorReport) -> Dict[str, float]:
        return {
            'max_eigenvalue_error': float(report.max_eigenvalue_error),
            'max_eigenvector_error': float(report.max_eigenvector_error),
            'avg_relative_error': float(report.avg_relative_error),
            'worst_condition_number': float(np.max(report.condition_numbers)),
            'best_condition_number': float(np.min(report.condition_numbers))
        }

    def get_processing_log(self) -> List[str]:
        return self.processing_log.copy()

    def clear_log(self):
        self.processing_log.clear()
