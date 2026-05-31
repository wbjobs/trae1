"""
迭代算法基类
定义求解器的通用接口和数据结构
提供内存安全的历史记录、自适应收敛判据、NaN检测和边界保护
"""

import numpy as np
from dataclasses import dataclass, field
from typing import List, Optional, Dict, Any, Tuple
from enum import Enum


class SolverStatus(Enum):
    CONVERGED = "converged"
    DIVERGED = "diverged"
    MAX_ITERATIONS = "max_iterations"
    SINGULAR_MATRIX = "singular_matrix"
    ERROR = "error"
    NAN_ENCOUNTERED = "nan_encountered"
    BOUNDARY_VIOLATION = "boundary_violation"


@dataclass
class IterationRecord:
    iteration: int
    x: np.ndarray
    residual: np.ndarray
    residual_norm: float
    step_norm: float


@dataclass
class SolverResult:
    solution: Optional[np.ndarray]
    status: SolverStatus
    iterations: int
    residual_norm: float
    history: List[IterationRecord] = field(default_factory=list)
    message: str = ""

    def to_dict(self) -> Dict[str, Any]:
        return {
            "solution": self.solution.tolist() if self.solution is not None else None,
            "status": self.status.value,
            "iterations": self.iterations,
            "residual_norm": self.residual_norm,
            "message": self.message,
            "history": [
                {
                    "iteration": rec.iteration,
                    "x": rec.x.tolist(),
                    "residual": rec.residual.tolist(),
                    "residual_norm": rec.residual_norm,
                    "step_norm": rec.step_norm,
                }
                for rec in self.history
            ],
        }


class BaseSolver:
    """求解器基类

    提供内存安全的历史记录管理、自适应收敛判据、
    NaN/Inf检测、边界保护和线搜索等通用功能。
    """

    def __init__(self, config=None):
        self.config = config
        self._history: List[IterationRecord] = []
        self._history_counter: int = 0
        self._initial_residual_norm: float = 0.0

    def solve(self, parser, x0: np.ndarray) -> SolverResult:
        raise NotImplementedError

    def _should_record(self, iteration: int) -> bool:
        if self.config.max_history_records == 0:
            return True
        if self.config.max_history_records < 0:
            return False
        if self.config.history_sampling_interval > 1:
            return (iteration % self.config.history_sampling_interval) == 0
        return len(self._history) < self.config.max_history_records

    def _record_iteration(self, iteration: int, x: np.ndarray,
                          residual: np.ndarray, step_norm: float):
        if not self._should_record(iteration):
            return

        if self.config.store_full_arrays:
            x_store = x.copy()
            r_store = residual.copy()
        else:
            x_store = np.array([])
            r_store = np.array([])

        record = IterationRecord(
            iteration=iteration,
            x=x_store,
            residual=r_store,
            residual_norm=float(np.linalg.norm(residual)),
            step_norm=float(step_norm),
        )
        self._history.append(record)

        if self.config.max_history_records > 0:
            if len(self._history) > self.config.max_history_records:
                self._history.pop(0)

    def _check_nan(self, x: np.ndarray, residual: np.ndarray) -> bool:
        if not self.config.enable_nan_check:
            return False
        if np.any(np.isnan(x)) or np.any(np.isnan(residual)):
            return True
        if np.any(np.isinf(x)) or np.any(np.isinf(residual)):
            return True
        return False

    def _check_boundary(self, x: np.ndarray) -> bool:
        if not self.config.enable_boundary_check:
            return False
        if np.any(x < self.config.variable_lower_bound):
            return True
        if np.any(x > self.config.variable_upper_bound):
            return True
        return False

    def _clip_to_boundary(self, x: np.ndarray) -> np.ndarray:
        if not self.config.enable_boundary_check:
            return x
        return np.clip(x, self.config.variable_lower_bound,
                       self.config.variable_upper_bound)

    def _check_convergence(self, residual: np.ndarray) -> bool:
        rms_norm = np.sqrt(np.mean(residual ** 2))
        abs_converged = rms_norm < self.config.rms_tolerance

        if self.config.use_relative_tolerance and self._initial_residual_norm > 1e-15:
            rel_norm = rms_norm / self._initial_residual_norm
            rel_converged = rel_norm < self.config.relative_tolerance
        else:
            rel_converged = True

        max_abs = np.max(np.abs(residual))
        max_converged = max_abs < self.config.tolerance

        return abs_converged and rel_converged and max_converged

    def _check_divergence(self, x: np.ndarray, residual: np.ndarray) -> bool:
        if np.any(np.abs(x) > self.config.divergence_threshold):
            return True
        if np.any(np.abs(residual) > self.config.divergence_threshold):
            return True
        rms = np.sqrt(np.mean(residual ** 2))
        if self._initial_residual_norm > 1e-15:
            if rms > self._initial_residual_norm * 1e6:
                return True
        return False

    def _backtracking_line_search(self, parser, x: np.ndarray,
                                   direction: np.ndarray,
                                   f_current: np.ndarray) -> Tuple[np.ndarray, float]:
        if not self.config.use_line_search:
            alpha = 1.0
            x_new = x + alpha * direction
            if self._check_boundary(x_new):
                x_new = self._clip_to_boundary(x_new)
            return x_new, alpha

        norm_f_current_sq = np.dot(f_current, f_current)
        alpha = 1.0
        x_new = x + alpha * direction

        for _ in range(self.config.max_line_search_iter):
            if self._check_boundary(x_new):
                x_new = self._clip_to_boundary(x_new)
                alpha = np.linalg.norm(x_new - x) / (np.linalg.norm(direction) + 1e-30)

            try:
                f_new = parser.evaluate(x_new)
            except Exception:
                alpha *= self.config.line_search_beta
                x_new = x + alpha * direction
                continue

            if self._check_nan(x_new, f_new):
                alpha *= self.config.line_search_beta
                x_new = x + alpha * direction
                continue

            norm_f_new_sq = np.dot(f_new, f_new)
            sufficient_decrease = self.config.line_search_alpha * alpha * (
                -2.0 * norm_f_current_sq
            ) if norm_f_current_sq > 1e-30 else -1e-30

            if norm_f_new_sq <= norm_f_current_sq + sufficient_decrease:
                return x_new, alpha

            if norm_f_new_sq < norm_f_current_sq:
                return x_new, alpha

            if alpha < self.config.min_step_size:
                break

            alpha *= self.config.line_search_beta
            x_new = x + alpha * direction

        return x_new, alpha

    def _solve_linear_system(self, jac: np.ndarray, rhs: np.ndarray) -> np.ndarray:
        n = len(rhs)
        try:
            delta = np.linalg.solve(jac, rhs)
        except np.linalg.LinAlgError:
            try:
                jac_reg = jac + self.config.jacobian_regularization * np.eye(n)
                delta = np.linalg.solve(jac_reg, rhs)
            except np.linalg.LinAlgError:
                delta = np.linalg.lstsq(jac, rhs, rcond=None)[0]

        step_norm = np.linalg.norm(delta)
        if step_norm > self.config.max_step_size:
            delta = delta * (self.config.max_step_size / step_norm)

        return delta

    def _set_initial_residual(self, residual: np.ndarray):
        self._initial_residual_norm = float(np.sqrt(np.mean(residual ** 2)))
