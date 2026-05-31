"""
拟牛顿法求解器
实现Broyden方法（好/坏）求解多元非线性方程组
采用逆雅可比近似更新，无需显式雅可比计算
支持线搜索、阻尼策略、边界保护等鲁棒性增强
"""

import numpy as np
from typing import Optional
from .base import BaseSolver, SolverResult, SolverStatus


class QuasiNewtonSolver(BaseSolver):
    """拟牛顿法求解器

    实现Broyden方法用于求解非线性方程组F(x)=0。
    与BFGS/DFP（优化算法）不同，Broyden方法专为求根问题设计，
    通过迭代更新逆雅可比近似避免显式雅可比计算。
    """

    def __init__(self, config=None, method: str = "broyden"):
        super().__init__(config)
        self.method = method.lower()
        if self.method not in ("broyden", "broyden_good", "broyden_bad"):
            raise ValueError(
                f"不支持的拟牛顿方法: {method}，请使用 'broyden', 'broyden_good', 'broyden_bad'"
            )

    def _init_inverse_jacobian(self, parser, x: np.ndarray) -> np.ndarray:
        n = len(x)
        try:
            jac = parser.jacobian(x)
            try:
                H = np.linalg.inv(jac)
                if np.any(np.isnan(H)) or np.any(np.isinf(H)):
                    raise ValueError("逆雅可比包含NaN/Inf")
                cond = np.linalg.cond(jac)
                if cond > 1e12:
                    H = np.eye(n) * self.config.jacobian_regularization
            except np.linalg.LinAlgError:
                H = np.eye(n) / self.config.jacobian_regularization
        except Exception:
            H = np.eye(n)
        return H

    def _update_inverse_jacobian(self, H: np.ndarray, s: np.ndarray,
                                  y: np.ndarray) -> np.ndarray:
        ys = y @ s
        if abs(ys) < 1e-14:
            return H

        if self.method == "broyden_bad":
            denom = y @ y
            if abs(denom) < 1e-14:
                return H
            correction = np.outer(s - H @ y, s) / denom
            H_new = H + correction
        else:
            denom = s @ H @ y
            if abs(denom) < 1e-14:
                denom = ys
            if abs(denom) < 1e-14:
                return H
            correction = np.outer(s - H @ y, s @ H) / denom
            H_new = H + correction

        max_val = np.max(np.abs(H_new))
        if max_val > self.config.divergence_threshold:
            H_new = H_new / (max_val / self.config.divergence_threshold)

        return H_new

    def solve(self, parser, x0: np.ndarray) -> SolverResult:
        n = len(x0)
        x = np.asarray(x0, dtype=float).copy()
        x = self._clip_to_boundary(x)
        self._history = []
        self._initial_residual_norm = 0.0
        damp = self.config.damping_factor if self.config.enable_damping else 1.0

        try:
            f_val = parser.evaluate(x)
            if self._check_nan(x, f_val):
                return SolverResult(
                    solution=None,
                    status=SolverStatus.NAN_ENCOUNTERED,
                    iterations=0,
                    residual_norm=float('inf'),
                    history=self._history.copy(),
                    message="初始点产生 NaN/Inf",
                )
            self._set_initial_residual(f_val)
        except Exception as e:
            return SolverResult(
                solution=None,
                status=SolverStatus.ERROR,
                iterations=0,
                residual_norm=float('inf'),
                history=self._history.copy(),
                message=f"初始点函数求值失败: {str(e)}",
            )

        H = self._init_inverse_jacobian(parser, x)
        f_prev = f_val.copy()
        x_prev = x.copy()

        for k in range(self.config.max_iterations):
            residual_norm = float(np.linalg.norm(f_val))

            if self._check_convergence(f_val):
                self._record_iteration(k, x, f_val, 0.0)
                return SolverResult(
                    solution=x,
                    status=SolverStatus.CONVERGED,
                    iterations=k + 1,
                    residual_norm=residual_norm,
                    history=self._history.copy(),
                    message=f"收敛于第 {k + 1} 次迭代 ({self.method.upper()})",
                )

            try:
                direction = -H @ f_val
            except Exception as e:
                return SolverResult(
                    solution=x,
                    status=SolverStatus.ERROR,
                    iterations=k,
                    residual_norm=residual_norm,
                    history=self._history.copy(),
                    message=f"方向计算失败: {str(e)}",
                )

            if self.config.enable_damping:
                direction = damp * direction

            x_new, alpha = self._backtracking_line_search(parser, x, direction, f_val)

            step_norm = float(np.linalg.norm(x_new - x))
            self._record_iteration(k, x, f_val, step_norm)

            if self._check_nan(x_new, np.zeros_like(x_new)):
                if self.config.nan_recovery_strategy == "damp":
                    damp *= 0.5
                    if damp < 1e-20:
                        return SolverResult(
                            solution=x,
                            status=SolverStatus.NAN_ENCOUNTERED,
                            iterations=k + 1,
                            residual_norm=residual_norm,
                            history=self._history.copy(),
                            message="阻尼恢复失败，迭代终止",
                        )
                    continue
                else:
                    return SolverResult(
                        solution=x,
                        status=SolverStatus.NAN_ENCOUNTERED,
                        iterations=k + 1,
                        residual_norm=residual_norm,
                        history=self._history.copy(),
                        message="迭代出现 NaN/Inf",
                    )

            if self._check_divergence(x_new, f_val):
                if self.config.enable_damping and self.config.use_adaptive_damping:
                    damp *= 0.5
                    if damp < 1e-20:
                        return SolverResult(
                            solution=x,
                            status=SolverStatus.DIVERGED,
                            iterations=k + 1,
                            residual_norm=residual_norm,
                            history=self._history.copy(),
                            message="迭代发散",
                        )
                    continue
                return SolverResult(
                    solution=x,
                    status=SolverStatus.DIVERGED,
                    iterations=k + 1,
                    residual_norm=residual_norm,
                    history=self._history.copy(),
                    message="迭代发散",
                )

            if step_norm < self.config.min_step_size:
                f_check = parser.evaluate(x_new)
                if self._check_convergence(f_check):
                    return SolverResult(
                        solution=x_new,
                        status=SolverStatus.CONVERGED,
                        iterations=k + 1,
                        residual_norm=float(np.linalg.norm(f_check)),
                        history=self._history.copy(),
                        message=f"收敛于第 {k + 1} 次迭代 ({self.method.upper()}, 步长极小)",
                    )

            try:
                f_new = parser.evaluate(x_new)
            except Exception as e:
                return SolverResult(
                    solution=x_new,
                    status=SolverStatus.ERROR,
                    iterations=k + 1,
                    residual_norm=residual_norm,
                    history=self._history.copy(),
                    message=f"函数求值失败: {str(e)}",
                )

            s = x_new - x_prev
            y = f_new - f_prev
            H = self._update_inverse_jacobian(H, s, y)

            if self.config.use_adaptive_damping and alpha > 0.9:
                damp = min(1.0, damp * 1.1)
            elif alpha < 0.1 and self.config.enable_damping:
                damp = max(0.01, damp * 0.8)

            x_prev = x.copy()
            f_prev = f_val.copy()
            x = x_new
            f_val = f_new

        f_final = parser.evaluate(x)
        return SolverResult(
            solution=x,
            status=SolverStatus.MAX_ITERATIONS,
            iterations=self.config.max_iterations,
            residual_norm=float(np.linalg.norm(f_final)),
            history=self._history.copy(),
            message=f"达到最大迭代次数 ({self.config.max_iterations})",
        )
