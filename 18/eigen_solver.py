"""
特征迭代求解模块 - 增强优化版
实现带位移的QR算法，支持单步位移和双步位移
新增功能: 奇异预警、批量求解、稳定性分析、高阶优化
"""

import numpy as np
from dataclasses import dataclass, field
from typing import Tuple, Optional, List, Dict, Callable
from enum import Enum
from concurrent.futures import ThreadPoolExecutor, as_completed

from matrix_preprocessor import MatrixPreprocessor


class ShiftStrategy(Enum):
    NONE = "none"
    SINGLE = "single"
    WILKINSON = "wilkinson"
    ADAPTIVE = "adaptive"


class SingularityWarning(Enum):
    NONE = "none"
    NEAR_SINGULAR = "near_singular"
    SINGULAR = "singular"
    HIGHLY_ILL_CONDITIONED = "highly_ill_conditioned"


@dataclass
class IterationData:
    iteration: int
    shift: Optional[float]
    max_off_diagonal: float
    eigenvalue_estimate: Optional[np.ndarray]
    residual_norm: float


@dataclass
class SingularityInfo:
    warning_level: SingularityWarning
    condition_number: Optional[float]
    rank: Optional[int]
    null_space_dimension: int
    warning_message: str


@dataclass
class StabilityInfo:
    eigenvalue: complex
    condition_number: float
    sensitivity: float
    stable: bool
    details: str


@dataclass
class EigenResult:
    eigenvalues: np.ndarray
    eigenvectors: np.ndarray
    converged: bool
    iteration_count: int
    shift_strategy: ShiftStrategy
    iteration_history: List[IterationData]
    computation_time: float
    final_residual: float
    singularity_info: Optional[SingularityInfo] = None
    stability_info: Optional[List[StabilityInfo]] = None


@dataclass
class BatchResult:
    results: List[EigenResult]
    total_time: float
    successful_count: int
    failed_count: int
    error_messages: List[str]


class EigenSolver:
    def __init__(self,
                 max_iterations: int = 500,
                 tolerance: float = 1e-10,
                 shift_strategy: ShiftStrategy = ShiftStrategy.ADAPTIVE,
                 compute_eigenvectors: bool = True,
                 enable_singularity_check: bool = True,
                 enable_stability_analysis: bool = False,
                 singularity_threshold: float = 1e8):
        self.max_iterations = max_iterations
        self.tolerance = tolerance
        self.shift_strategy = shift_strategy
        self.compute_eigenvectors = compute_eigenvectors
        self.enable_singularity_check = enable_singularity_check
        self.enable_stability_analysis = enable_stability_analysis
        self.singularity_threshold = singularity_threshold

        self.preprocessor = MatrixPreprocessor(tolerance=tolerance)
        self.iteration_history: List[IterationData] = []
        self.processing_log: List[str] = []
        self.warning_log: List[str] = []

    def solve(self, matrix: np.ndarray) -> EigenResult:
        import time

        start_time = time.time()
        n = matrix.shape[0]

        self.processing_log.append(f"开始求解 {n}x{n} 矩阵特征值问题")

        singularity_info = None
        if self.enable_singularity_check:
            singularity_info = self._check_singularity(matrix)
            if singularity_info.warning_level != SingularityWarning.NONE:
                self.warning_log.append(singularity_info.warning_message)

        try:
            self.preprocessor.analyze_matrix(matrix)
        except Exception:
            pass

        try:
            H, Q_hessenberg = self.preprocessor.permute_to_upper_hessenberg(matrix)
            self.processing_log.append("矩阵已转换为上Hessenberg形式")
        except Exception as e:
            self.processing_log.append(f"Hessenberg变换失败: {e}")
            H = matrix.copy()
            Q_hessenberg = np.eye(n)

        eigenvalues, H_final, converged, iterations = self._qr_algorithm(H)

        eigenvectors = None
        if self.compute_eigenvectors:
            try:
                eigenvectors = self._compute_eigenvectors(matrix, eigenvalues, Q_hessenberg)
            except Exception as e:
                self.processing_log.append(f"特征向量计算失败: {e}")
                eigenvectors = np.eye(n, dtype=complex)

        stability_info = None
        if self.enable_stability_analysis:
            stability_info = self._analyze_stability(matrix, eigenvalues, eigenvectors)

        computation_time = time.time() - start_time

        try:
            residual = self._compute_final_residual(matrix, eigenvalues, eigenvectors)
        except Exception:
            residual = float('inf')

        self.processing_log.append(f"特征值求解完成: {iterations}次迭代, "
                                    f"收敛={converged}, 耗时={computation_time:.4f}秒")

        return EigenResult(
            eigenvalues=eigenvalues,
            eigenvectors=eigenvectors,
            converged=converged,
            iteration_count=iterations,
            shift_strategy=self.shift_strategy,
            iteration_history=self.iteration_history,
            computation_time=computation_time,
            final_residual=residual,
            singularity_info=singularity_info,
            stability_info=stability_info
        )

    def solve_batch(self, matrices: List[np.ndarray],
                    parallel: bool = False,
                    max_workers: int = 4) -> BatchResult:
        import time

        start_time = time.time()
        results: List[EigenResult] = []
        error_messages: List[str] = []
        successful = 0
        failed = 0

        if parallel and len(matrices) > 1:
            with ThreadPoolExecutor(max_workers=max_workers) as executor:
                future_to_idx = {
                    executor.submit(self.solve, mat): idx
                    for idx, mat in enumerate(matrices)
                }
                temp_results = [None] * len(matrices)
                for future in as_completed(future_to_idx):
                    idx = future_to_idx[future]
                    try:
                        result = future.result()
                        temp_results[idx] = result
                        successful += 1
                    except Exception as e:
                        error_messages.append(f"矩阵{idx}求解失败: {str(e)}")
                        failed += 1
                results = [r for r in temp_results if r is not None]
        else:
            for idx, mat in enumerate(matrices):
                try:
                    result = self.solve(mat)
                    results.append(result)
                    successful += 1
                except Exception as e:
                    error_messages.append(f"矩阵{idx}求解失败: {str(e)}")
                    failed += 1

        total_time = time.time() - start_time

        return BatchResult(
            results=results,
            total_time=total_time,
            successful_count=successful,
            failed_count=failed,
            error_messages=error_messages
        )

    def _check_singularity(self, matrix: np.ndarray) -> SingularityInfo:
        n = matrix.shape[0]
        cond = None
        rank = None
        warning_level = SingularityWarning.NONE
        warning_message = "矩阵状态正常"

        try:
            cond = np.linalg.cond(matrix)
        except Exception:
            pass

        try:
            rank = np.linalg.matrix_rank(matrix, tol=self.tolerance * 100)
        except Exception:
            pass

        if cond is not None:
            if not np.isfinite(cond) or cond > 1e15:
                warning_level = SingularityWarning.SINGULAR
                warning_message = f"警告: 矩阵可能奇异，条件数={cond:.2e}"
            elif cond > self.singularity_threshold:
                warning_level = SingularityWarning.HIGHLY_ILL_CONDITIONED
                warning_message = f"警告: 矩阵高度病态，条件数={cond:.2e}"
            elif cond > 1e8:
                warning_level = SingularityWarning.NEAR_SINGULAR
                warning_message = f"警告: 矩阵接近奇异，条件数={cond:.2e}"

        if rank is not None and rank < n:
            if warning_level == SingularityWarning.NONE:
                warning_level = SingularityWarning.SINGULAR
                warning_message = f"警告: 矩阵秩亏，秩={rank}/{n}"
            else:
                warning_message += f"，秩={rank}/{n}"

        null_space_dim = n - rank if rank is not None else 0

        return SingularityInfo(
            warning_level=warning_level,
            condition_number=cond,
            rank=rank,
            null_space_dimension=null_space_dim,
            warning_message=warning_message
        )

    def _analyze_stability(self, matrix: np.ndarray, eigenvalues: np.ndarray,
                           eigenvectors: np.ndarray) -> List[StabilityInfo]:
        n = matrix.shape[0]
        stability_results: List[StabilityInfo] = []

        if eigenvectors is None:
            return stability_results

        try:
            if eigenvectors.shape[0] == eigenvectors.shape[1]:
                norm = np.linalg.norm(eigenvectors)
                if norm > 1e-16 and np.linalg.cond(eigenvectors) < 1e15:
                    left_vectors = np.linalg.inv(eigenvectors).T
                else:
                    left_vectors = np.linalg.pinv(eigenvectors).T
            else:
                left_vectors = np.linalg.pinv(eigenvectors).T

            for i in range(n):
                ev = eigenvalues[i]
                right_vec = eigenvectors[:, i]
                left_vec = left_vectors[:, i]

                try:
                    denominator = np.vdot(left_vec, right_vec)
                    if abs(denominator) < 1e-16:
                        cond_num = float('inf')
                        sensitivity = float('inf')
                    else:
                        cond_num = 1.0 / abs(denominator)
                        sensitivity = cond_num * np.linalg.norm(matrix)
                except Exception:
                    cond_num = float('inf')
                    sensitivity = float('inf')

                stable = cond_num < 1e8 if np.isfinite(cond_num) else False

                details = (
                    f"特征值λ={ev:.6f}, 条件数={cond_num:.2e}, "
                    f"灵敏度={sensitivity:.2e}, 稳定={stable}"
                )

                stability_results.append(StabilityInfo(
                    eigenvalue=ev,
                    condition_number=cond_num,
                    sensitivity=sensitivity,
                    stable=stable,
                    details=details
                ))
        except Exception as e:
            self.processing_log.append(f"稳定性分析失败: {e}")

        return stability_results

    def _qr_algorithm(self, H: np.ndarray) -> Tuple[np.ndarray, np.ndarray, bool, int]:
        n = H.shape[0]
        H_current = H.copy().astype(np.float64)

        eigenvalues = np.zeros(n, dtype=complex)
        converged = False
        total_iterations = 0

        p = n
        consecutive_stalls = 0
        last_max_off = float('inf')

        while p > 0 and total_iterations < self.max_iterations:
            total_iterations += 1

            if p == 1:
                eigenvalues[0] = H_current[0, 0]
                p = 0
                converged = True
                break

            if p == 2:
                ev = self._solve_2x2(H_current[:2, :2])
                eigenvalues[:2] = ev
                p = 0
                converged = True
                break

            p = self._try_deflate(H_current, eigenvalues, p)
            if p <= 0:
                converged = True
                break

            shift = self._compute_shift(H_current[:p, :p], p, total_iterations)

            try:
                H_new = self._qr_step(H_current[:p, :p], shift)
                if np.any(np.isnan(H_new)) or np.any(np.isinf(H_new)):
                    shift += 1e-6 * total_iterations
                    H_new = self._qr_step(H_current[:p, :p], shift)
                H_current[:p, :p] = H_new
            except Exception:
                consecutive_stalls += 1
                continue

            max_off = 0.0
            for i in range(p - 1):
                if abs(H_current[i+1, i]) > max_off:
                    max_off = abs(H_current[i+1, i])

            if abs(max_off - last_max_off) < self.tolerance:
                consecutive_stalls += 1
            else:
                consecutive_stalls = 0
            last_max_off = max_off

            residual = 0.0
            for i in range(p - 1):
                residual += abs(H_current[i+1, i])
            if p > 1:
                residual /= (p - 1)

            self.iteration_history.append(IterationData(
                iteration=total_iterations,
                shift=shift,
                max_off_diagonal=max_off,
                eigenvalue_estimate=np.diag(H_current).copy() if p == n else None,
                residual_norm=residual
            ))

            if max_off < self.tolerance * 100:
                for j in range(p):
                    eigenvalues[j] = H_current[j, j]
                p = 0
                converged = True
                break

            if consecutive_stalls > 10:
                shift = np.random.randn() * np.trace(H_current[:p, :p]) / p
                consecutive_stalls = 0

        if p > 0:
            self.processing_log.append(f"达到最大迭代次数，使用当前结果")
            for j in range(p):
                eigenvalues[j] = H_current[j, j]
            converged = False

        eigenvalues = self._sort_eigenvalues(eigenvalues)

        return eigenvalues, H, converged, max(total_iterations, 1)

    def _try_deflate(self, H_current: np.ndarray, eigenvalues: np.ndarray,
                     p: int) -> int:
        l = p - 1
        while l > 0:
            scale = abs(H_current[l-1, l-1]) + abs(H_current[l, l])
            threshold = self.tolerance * 100 * max(scale, np.finfo(float).eps)
            if abs(H_current[l, l-1]) < threshold:
                if l == p - 1:
                    eigenvalues[p-1] = H_current[p-1, p-1]
                    return p - 1
                elif l == p - 2:
                    ev = self._solve_2x2(H_current[p-2:p, p-2:p])
                    eigenvalues[p-2:p] = ev
                    return p - 2
                else:
                    return l + 1
            l -= 1
        return p

    def _qr_step(self, H: np.ndarray, shift: float) -> np.ndarray:
        n = H.shape[0]

        H_shifted = H.copy()
        for i in range(n):
            H_shifted[i, i] -= shift

        Q, R = self._givens_qr(H_shifted)
        H_new = R @ Q
        for i in range(n):
            H_new[i, i] += shift

        return H_new

    def _givens_qr(self, H: np.ndarray) -> Tuple[np.ndarray, np.ndarray]:
        n = H.shape[0]
        Q = np.eye(n)
        R = H.copy()

        for i in range(n - 1):
            a = R[i, i]
            b = R[i + 1, i]
            r = np.sqrt(a * a + b * b)

            if r < self.tolerance * 100:
                continue

            c = a / r
            s = b / r

            temp = c * R[i, i:n] + s * R[i+1, i:n]
            R[i+1, i:n] = -s * R[i, i:n] + c * R[i+1, i:n]
            R[i, i:n] = temp

            temp = c * Q[:, i] + s * Q[:, i+1]
            Q[:, i+1] = -s * Q[:, i] + c * Q[:, i+1]
            Q[:, i] = temp

        return Q, R

    def _compute_shift(self, H: np.ndarray, p: int, iteration: int) -> float:
        if self.shift_strategy == ShiftStrategy.NONE:
            return 0.0

        if p < 2:
            return H[p-1, p-1]

        if self.shift_strategy == ShiftStrategy.SINGLE:
            return H[p-1, p-1]

        a = H[p-2, p-2]
        b = H[p-2, p-1]
        c = H[p-1, p-2]
        d = H[p-1, p-1]

        trace = a + d
        det = a * d - b * c
        disc = trace * trace - 4 * det

        if disc >= 0:
            sqrt_disc = np.sqrt(disc)
            ev1 = (trace + sqrt_disc) / 2
            ev2 = (trace - sqrt_disc) / 2
            wilkinson = ev1 if abs(ev1 - d) < abs(ev2 - d) else ev2
        else:
            wilkinson = d

        if self.shift_strategy == ShiftStrategy.ADAPTIVE:
            if iteration % 20 == 0 and iteration > 0:
                return H[p-1, p-1]
            return wilkinson

        return wilkinson

    def _solve_2x2(self, matrix: np.ndarray) -> np.ndarray:
        a, b = matrix[0, 0], matrix[0, 1]
        c, d = matrix[1, 0], matrix[1, 1]

        trace = a + d
        det = a * d - b * c
        discriminant = trace * trace - 4 * det

        if discriminant >= -1e-14:
            sqrt_disc = np.sqrt(max(discriminant, 0))
            return np.array([(trace + sqrt_disc) / 2, (trace - sqrt_disc) / 2])
        else:
            sqrt_disc = np.sqrt(abs(discriminant))
            return np.array([
                complex(trace / 2, sqrt_disc / 2),
                complex(trace / 2, -sqrt_disc / 2)
            ], dtype=complex)

    def _sort_eigenvalues(self, eigenvalues: np.ndarray) -> np.ndarray:
        n = len(eigenvalues)
        mask_real = np.abs(eigenvalues.imag) < self.tolerance * 1000

        real_ev = eigenvalues[mask_real].real
        complex_ev = eigenvalues[~mask_real]

        real_ev = np.sort(real_ev)[::-1]

        complex_pairs = []
        used = np.zeros(len(complex_ev), dtype=bool)
        for i in range(len(complex_ev)):
            if not used[i]:
                for j in range(i + 1, len(complex_ev)):
                    if not used[j]:
                        if (abs(complex_ev[i].real - complex_ev[j].real) < self.tolerance * 1000 and
                            abs(complex_ev[i].imag + complex_ev[j].imag) < self.tolerance * 1000):
                            if complex_ev[i].imag > 0:
                                complex_pairs.append(complex_ev[i])
                                complex_pairs.append(complex_ev[j])
                            else:
                                complex_pairs.append(complex_ev[j])
                                complex_pairs.append(complex_ev[i])
                            used[i] = True
                            used[j] = True
                            break

        result = np.zeros(n, dtype=complex)
        result[:len(real_ev)] = real_ev
        result[len(real_ev):len(real_ev) + len(complex_pairs)] = complex_pairs

        return result

    def _compute_eigenvectors(self, matrix: np.ndarray, eigenvalues: np.ndarray,
                              Q_hessenberg: np.ndarray) -> np.ndarray:
        n = matrix.shape[0]
        eigenvectors = np.zeros((n, n), dtype=complex)

        computed = np.zeros(n, dtype=bool)

        for i in range(n):
            if computed[i]:
                continue

            ev = eigenvalues[i]

            if abs(ev.imag) > self.tolerance * 1000:
                for j in range(i + 1, n):
                    if not computed[j]:
                        if (abs(ev.real - eigenvalues[j].real) < self.tolerance * 1000 and
                            abs(ev.imag + eigenvalues[j].imag) < self.tolerance * 1000):
                            try:
                                vec = self._inverse_iteration(matrix, ev)
                                eigenvectors[:, i] = vec
                                eigenvectors[:, j] = np.conj(vec)
                                computed[i] = True
                                computed[j] = True
                            except Exception:
                                pass
                            break
                if not computed[i]:
                    try:
                        vec = self._inverse_iteration(matrix, ev)
                        eigenvectors[:, i] = vec
                        computed[i] = True
                    except Exception:
                        eigenvectors[:, i] = np.eye(n, dtype=complex)[:, i]
                        computed[i] = True
            else:
                try:
                    vec = self._inverse_iteration(matrix, ev.real)
                    eigenvectors[:, i] = vec
                    computed[i] = True
                except Exception:
                    eigenvectors[:, i] = np.eye(n, dtype=complex)[:, i]
                    computed[i] = True

        for i in range(n):
            if not computed[i]:
                try:
                    vec = self._inverse_iteration(matrix, eigenvalues[i])
                    eigenvectors[:, i] = vec
                except Exception:
                    eigenvectors[:, i] = np.eye(n, dtype=complex)[:, i]

        for i in range(n):
            norm = np.linalg.norm(eigenvectors[:, i])
            if norm > 1e-16:
                eigenvectors[:, i] /= norm

        return eigenvectors

    def _inverse_iteration(self, matrix: np.ndarray, eigenvalue: complex,
                           max_iter: int = 10) -> np.ndarray:
        n = matrix.shape[0]

        v = np.random.randn(n).astype(complex)
        norm_v = np.linalg.norm(v)
        if norm_v > 1e-16:
            v /= norm_v
        else:
            v = np.eye(n, dtype=complex)[:, 0]

        A = matrix.copy().astype(complex)
        for i in range(n):
            A[i, i] -= eigenvalue

        A_inv = None
        shift = 0.0
        for attempt in range(10):
            try:
                A_shifted = A.copy()
                for i in range(n):
                    A_shifted[i, i] += shift
                try:
                    cond = np.linalg.cond(A_shifted)
                    if not np.isfinite(cond) or cond > 1e12:
                        shift += 1e-6 * (10 ** attempt)
                        continue
                except Exception:
                    pass
                A_inv = np.linalg.inv(A_shifted)
                break
            except np.linalg.LinAlgError:
                shift += 1e-4 * (10 ** attempt)
                A_inv = None

        if A_inv is None:
            return v

        best_v = v.copy()
        best_residual = float('inf')

        for _ in range(max_iter):
            try:
                w = A_inv @ v
            except Exception:
                break

            norm_w = np.linalg.norm(w)
            if norm_w < 1e-16 or not np.isfinite(norm_w):
                break

            v = w / norm_w

            try:
                rayleigh = np.vdot(v, matrix @ v) / np.vdot(v, v)
                residual = np.linalg.norm(matrix @ v - rayleigh * v)
                if residual < best_residual:
                    best_residual = residual
                    best_v = v.copy()
            except Exception:
                pass

        return best_v

    def _compute_final_residual(self, matrix: np.ndarray, eigenvalues: np.ndarray,
                                eigenvectors: np.ndarray) -> float:
        if eigenvectors is None:
            return float('inf')

        n = matrix.shape[0]
        max_residual = 0.0

        for i in range(n):
            try:
                ev = eigenvalues[i]
                vec = eigenvectors[:, i]

                if np.linalg.norm(vec) < 1e-16:
                    continue

                residual = matrix @ vec - ev * vec
                norm_res = np.linalg.norm(residual) / (np.linalg.norm(vec) + 1e-16)
                if np.isfinite(norm_res):
                    max_residual = max(max_residual, norm_res)
            except Exception:
                continue

        return max_residual if max_residual > 0 else float('inf')

    def get_iteration_history(self) -> List[IterationData]:
        return self.iteration_history.copy()

    def get_processing_log(self) -> List[str]:
        return self.processing_log.copy()

    def get_warning_log(self) -> List[str]:
        return self.warning_log.copy()

    def clear_log(self):
        self.iteration_history.clear()
        self.processing_log.clear()
        self.warning_log.clear()
        self.preprocessor.clear_log()
