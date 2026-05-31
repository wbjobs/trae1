"""
牛顿迭代法求解器
实现带线搜索、阻尼策略、信赖域和预条件的牛顿迭代法
x_{k+1} = x_k - alpha_k * J(x_k)^{-1} * F(x_k)
支持高维方程组、边界参数和数值不稳定情形
增加预条件、自适应正则化和收敛加速策略
"""

import numpy as np
from typing import Optional, Tuple
from .base import BaseSolver, SolverResult, SolverStatus


class NewtonSolver(BaseSolver):
    """牛顿迭代法求解器

    经典牛顿法的增强实现，加入回溯线搜索、自适应阻尼、
    边界保护、雅可比正则化、信赖域策略和预条件技术，
    显著提升高维和边界问题的鲁棒性和收敛速度。
    """

    def __init__(self, config=None):
        super().__init__(config)
        self._trust_radius = 1.0
        self._preconditioner = None
        self._consecutive_success = 0

    def _compute_preconditioner(self, jac: np.ndarray) -> np.ndarray:
        """计算雅可比预条件器（对角矩阵）"""
        n = jac.shape[0]
        diag = np.diag(jac)
        diag = np.where(np.abs(diag) > 1e-15, diag, 1.0)
        M = np.diag(1.0 / diag)
        return M

    def _apply_preconditioner(self, M: np.ndarray,
                               vec: np.ndarray) -> np.ndarray:
        """应用预条件器"""
        return M @ vec

    def _trust_region_solve(self, jac: np.ndarray,
                            rhs: np.ndarray,
                            radius: float,
                            preconditioner: Optional[np.ndarray] = None
                            ) -> np.ndarray:
        n = len(rhs)

        if preconditioner is not None:
            jac_prec = preconditioner @ jac
            rhs_prec = preconditioner @ rhs
        else:
            jac_prec = jac
            rhs_prec = rhs

        try:
            delta = np.linalg.solve(jac_prec, rhs_prec)
        except np.linalg.LinAlgError:
            try:
                jac_reg = jac_prec + self.config.jacobian_regularization * np.eye(n)
                delta = np.linalg.solve(jac_reg, rhs_prec)
            except np.linalg.LinAlgError:
                delta = np.linalg.lstsq(jac_prec, rhs_prec, rcond=None)[0]

        if np.any(np.isnan(delta)) or np.any(np.isinf(delta)):
            delta = np.linalg.lstsq(jac_prec, rhs_prec, rcond=None)[0]

        delta_norm = np.linalg.norm(delta)
        if delta_norm > radius:
            delta = delta * (radius / delta_norm)

        return delta

    def _adaptive_regularization(self, jac: np.ndarray,
                                  f_val: np.ndarray,
                                  iteration: int) -> float:
        """自适应正则化参数"""
        base_reg = self.config.jacobian_regularization
        cond = np.linalg.cond(jac) if jac.size > 0 else 1.0

        if cond > 1e12:
            return base_reg * 10
        elif cond > 1e10:
            return base_reg * 5
        elif cond > 1e8:
            return base_reg * 2
        else:
            return base_reg

    def _accelerate_convergence(self, x: np.ndarray,
                                 f_val: np.ndarray,
                                 x_prev: np.ndarray,
                                 f_prev: np.ndarray,
                                 iteration: int) -> Optional[np.ndarray]:
        """Aitken加速或Steffensen加速"""
        if iteration < 2:
            return None

        try:
            s = x - x_prev
            if np.linalg.norm(s) < 1e-15:
                return None

            fs = f_val - f_prev
            denom = np.dot(s, s)
            if abs(denom) < 1e-30:
                return None

            gamma = np.dot(s, fs) / denom

            if abs(gamma) < 1e-10 or abs(gamma - 1.0) < 1e-10:
                return None

            acceleration = 1.0 / (1.0 - gamma)
            if 0.5 < acceleration < 3.0:
                x_accelerated = x_prev + acceleration * (x - x_prev)
                return x_accelerated
        except Exception:
            pass

        return None

    def solve(self, parser, x0: np.ndarray) -> SolverResult:
        x = np.asarray(x0, dtype=float).copy()
        x = self._clip_to_boundary(x)
        self._history = []
        self._initial_residual_norm = 0.0
        self._trust_radius = 1.0
        self._consecutive_success = 0
        damp = self.config.damping_factor if self.config.enable_damping else 1.0

        x_prev = x.copy()
        f_prev = None

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

        consecutive_failures = 0

        for k in range(self.config.max_iterations):
            try:
                f_val = parser.evaluate(x)
                jac = parser.jacobian(x)
            except Exception as e:
                return SolverResult(
                    solution=x,
                    status=SolverStatus.ERROR,
                    iterations=k,
                    residual_norm=float(np.linalg.norm(f_val)),
                    history=self._history.copy(),
                    message=f"函数求值失败: {str(e)}",
                )

            residual_norm = float(np.linalg.norm(f_val))
            self._record_iteration(k, x, f_val, 0.0)

            if self._check_nan(x, f_val):
                return SolverResult(
                    solution=x,
                    status=SolverStatus.NAN_ENCOUNTERED,
                    iterations=k + 1,
                    residual_norm=residual_norm,
                    history=self._history.copy(),
                    message=f"第 {k + 1} 次迭代出现 NaN/Inf",
                )

            if self._check_convergence(f_val):
                return SolverResult(
                    solution=x,
                    status=SolverStatus.CONVERGED,
                    iterations=k + 1,
                    residual_norm=residual_norm,
                    history=self._history.copy(),
                    message=f"收敛于第 {k + 1} 次迭代",
                )

            if k > 0 and f_prev is not None and self._consecutive_success >= 3:
                x_accel = self._accelerate_convergence(
                    x, f_val, x_prev, f_prev, k
                )
                if x_accel is not None:
                    try:
                        f_accel = parser.evaluate(x_accel)
                        if np.linalg.norm(f_accel) < residual_norm * 0.9:
                            x = x_accel
                            f_val = f_accel
                            residual_norm = float(np.linalg.norm(f_val))
                            if self._check_convergence(f_val):
                                return SolverResult(
                                    solution=x,
                                    status=SolverStatus.CONVERGED,
                                    iterations=k + 1,
                                    residual_norm=residual_norm,
                                    history=self._history.copy(),
                                    message=f"收敛于第 {k + 1} 次迭代 (加速)",
                                )
                    except Exception:
                        pass

            M = None
            if k >= 2 and self._consecutive_success >= 2:
                try:
                    M = self._compute_preconditioner(jac)
                except Exception:
                    M = None

            try:
                delta = self._trust_region_solve(
                    jac, -f_val, self._trust_radius, M
                )
            except Exception as e:
                return SolverResult(
                    solution=x,
                    status=SolverStatus.SINGULAR_MATRIX,
                    iterations=k + 1,
                    residual_norm=residual_norm,
                    history=self._history.copy(),
                    message=f"线性系统求解失败: {str(e)}",
                )

            if self.config.enable_damping:
                delta = damp * delta

            step_norm = float(np.linalg.norm(delta))
            if step_norm < self.config.min_step_size:
                delta = delta * (self.config.min_step_size / step_norm + 1e-30)
                step_norm = float(np.linalg.norm(delta))

            x_new, alpha = self._backtracking_line_search(parser, x, delta, f_val)
            actual_step = float(np.linalg.norm(x_new - x))
            self._history[-1].step_norm = actual_step

            try:
                f_new = parser.evaluate(x_new)
                predicted = np.dot(f_val, -jac @ delta)
                actual = np.dot(f_val, f_val) - np.dot(f_new, f_new)

                if abs(predicted) > 1e-15:
                    ratio = actual / predicted
                    if ratio > 0.75:
                        self._trust_radius = min(
                            self.config.max_step_size,
                            max(self._trust_radius, 2.0 * actual_step),
                        )
                        consecutive_failures = 0
                        self._consecutive_success += 1
                    elif ratio > 0.25:
                        consecutive_failures = 0
                        self._consecutive_success += 1
                    else:
                        self._trust_radius = max(0.1, 0.25 * actual_step)
                        consecutive_failures += 1
                        self._consecutive_success = 0
                else:
                    consecutive_failures = 0
                    self._consecutive_success += 1
            except Exception:
                consecutive_failures += 1
                self._consecutive_success = 0

            if consecutive_failures >= 5:
                damp *= 0.5
                self._trust_radius *= 0.5
                consecutive_failures = 0
                self._consecutive_success = 0
                if damp < 1e-20 or self._trust_radius < 1e-20:
                    return SolverResult(
                        solution=x,
                        status=SolverStatus.DIVERGED,
                        iterations=k + 1,
                        residual_norm=residual_norm,
                        history=self._history.copy(),
                        message="连续失败，迭代终止",
                    )

            if self._check_nan(x_new, np.zeros_like(x_new)):
                if self.config.nan_recovery_strategy == "damp":
                    damp *= 0.5
                    self._trust_radius *= 0.5
                    self._consecutive_success = 0
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
                    self._trust_radius *= 0.5
                    self._consecutive_success = 0
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

            if actual_step < self.config.min_step_size:
                f_check = parser.evaluate(x_new)
                if self._check_convergence(f_check):
                    return SolverResult(
                        solution=x_new,
                        status=SolverStatus.CONVERGED,
                        iterations=k + 1,
                        residual_norm=float(np.linalg.norm(f_check)),
                        history=self._history.copy(),
                        message=f"收敛于第 {k + 1} 次迭代 (步长极小)",
                    )

            if self.config.use_adaptive_damping and alpha > 0.9:
                damp = min(1.0, damp * 1.1)
            elif alpha < 0.1 and self.config.enable_damping:
                damp = max(0.01, damp * 0.8)

            x_prev = x.copy()
            f_prev = f_val.copy()
            x = x_new

        f_final = parser.evaluate(x)
        return SolverResult(
            solution=x,
            status=SolverStatus.MAX_ITERATIONS,
            iterations=self.config.max_iterations,
            residual_norm=float(np.linalg.norm(f_final)),
            history=self._history.copy(),
            message=f"达到最大迭代次数 ({self.config.max_iterations})",
        )
