"""
复化梯形积分算法模块
================================

实现自适应复化梯形积分算法，支持：
- 自适应步长调整
- 区间细分
- 精度控制
- 计算超时保护
"""

from typing import Callable, List, Tuple, Optional
from dataclasses import dataclass
import math
import time

from .core_types import (
    IntegralConfig,
    IntegralResult,
    Interval,
    SubIntervalResult,
)


class AdaptiveTrapezoidal:
    """
    自适应复化梯形积分器

    使用逐步加密的梯形法则，自动调整步长以达到目标精度。
    """

    def __init__(
        self,
        function: Callable,
        config: Optional[IntegralConfig] = None
    ):
        self.function = function
        self.config = config or IntegralConfig()
        self._total_evaluations = 0
        self._convergence_history: List[float] = []
        self._start_time = 0.0
        self._max_time = 10.0

    def integrate(
        self,
        interval: Interval,
        n_initial_points: Optional[int] = None
    ) -> IntegralResult:
        """
        执行自适应梯形积分

        Args:
            interval: 积分区间
            n_initial_points: 初始点数（可选）

        Returns:
            积分结果
        """
        self._total_evaluations = 0
        self._convergence_history = []
        self._start_time = time.time()

        if not interval.is_finite:
            return self._integrate_infinite(interval)

        return self._integrate_finite(interval, n_initial_points)

    def _integrate_finite(
        self,
        interval: Interval,
        n_initial_points: Optional[int] = None
    ) -> IntegralResult:
        """
        有限区间积分

        Args:
            interval: 有限积分区间
            n_initial_points: 初始点数

        Returns:
            积分结果
        """
        a, b = interval.start, interval.end
        L = b - a

        if abs(L) < 1e-15:
            return IntegralResult(
                value=0.0,
                error_estimate=0.0,
                intervals_used=1,
                function_evaluations=0,
                iterations=0,
                converged=True,
                method="trapezoidal",
                interval=interval,
                config=self.config
            )

        n_points = n_initial_points or max(10, self.config.min_sample_points * 2)
        n_points = max(n_points, 2)

        values = []
        prev_result = None

        for iteration in range(self.config.max_iter):
            if time.time() - self._start_time > self._max_time:
                break

            h = L / (n_points - 1)
            result, evals = self._trapezoidal_rule(a, b, n_points)

            values.append(result)
            self._total_evaluations += evals
            self._convergence_history.append(result)

            if prev_result is not None:
                error = abs(result - prev_result)
                if error < self.config.tolerance * 0.01:
                    return IntegralResult(
                        value=result,
                        error_estimate=error,
                        intervals_used=n_points,
                        function_evaluations=self._total_evaluations,
                        iterations=iteration + 1,
                        converged=True,
                        method="trapezoidal",
                        interval=interval,
                        config=self.config
                    )

            prev_result = result
            n_points = n_points * 2
            n_points = min(n_points, 100000)

        final_value = values[-1] if values else 0.0
        final_error = abs(values[-1] - values[-2]) / 3.0 if len(values) >= 2 else float('inf')

        return IntegralResult(
            value=final_value,
            error_estimate=final_error,
            intervals_used=n_points,
            function_evaluations=self._total_evaluations,
            iterations=len(values),
            converged=final_error < self.config.tolerance,
            method="trapezoidal",
            interval=interval,
            config=self.config
        )

    def _integrate_infinite(
        self,
        interval: Interval
    ) -> IntegralResult:
        """
        无穷区间积分（使用变量替换）

        Args:
            interval: 无穷区间

        Returns:
            积分结果
        """
        f = self.function

        if interval.is_infinite_start and interval.is_infinite_end:
            def transformed(t: float) -> float:
                if abs(t) >= 1:
                    return 0.0
                x = math.tan(math.pi * t / 2)
                dx_dt = math.pi / (2 * math.cos(math.pi * t / 2) ** 2)
                try:
                    return f(x) * dx_dt
                except (ValueError, ZeroDivisionError, OverflowError):
                    return 0.0

            transformed_interval = Interval(start=-0.9999, end=0.9999)

        elif interval.is_infinite_start:
            b = interval.end
            def transformed(t: float) -> float:
                if t <= -0.9999:
                    return 0.0
                x = b + math.tan(math.pi * t / 2)
                dx_dt = math.pi / (2 * math.cos(math.pi * t / 2) ** 2)
                try:
                    return f(x) * dx_dt
                except (ValueError, ZeroDivisionError, OverflowError):
                    return 0.0

            transformed_interval = Interval(start=-0.9999, end=0.0)

        else:
            a = interval.start
            def transformed(t: float) -> float:
                if t >= 0.9999:
                    return 0.0
                x = a + math.tan(math.pi * t / 2)
                dx_dt = math.pi / (2 * math.cos(math.pi * t / 2) ** 2)
                try:
                    return f(x) * dx_dt
                except (ValueError, ZeroDivisionError, OverflowError):
                    return 0.0

            transformed_interval = Interval(start=0.0, end=0.9999)

        original_func = self.function
        self.function = transformed
        result = self._integrate_finite(transformed_interval, 100)
        self.function = original_func
        result.method = f"improper_{result.method}"

        return result

    def _trapezoidal_rule(
        self,
        a: float,
        b: float,
        n_points: int
    ) -> Tuple[float, int]:
        """
        复化梯形公式

        Args:
            a: 积分下限
            b: 积分上限
            n_points: 采样点数

        Returns:
            (积分近似值, 函数评估次数)
        """
        n_points = max(n_points, 2)
        h = (b - a) / (n_points - 1)

        result = 0.5 * (self._safe_eval(a) + self._safe_eval(b))
        evals = 2

        for i in range(1, n_points - 1):
            x = a + i * h
            result += self._safe_eval(x)
            evals += 1

        result *= h

        return result, evals

    def _safe_eval(self, x: float) -> float:
        """安全的函数求值"""
        try:
            val = self.function(x)
            if math.isnan(val) or math.isinf(val):
                return 0.0
            return val
        except (ValueError, ZeroDivisionError, OverflowError):
            return 0.0

    def get_convergence_history(self) -> List[float]:
        return self._convergence_history.copy()
