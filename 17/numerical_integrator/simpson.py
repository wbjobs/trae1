"""
复化辛普森积分算法模块
================================

实现自适应复化辛普森积分算法，支持：
- 自适应步长调整
- 区间细分
- 精度控制
- 龙贝格积分
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


class AdaptiveSimpson:
    """
    自适应复化辛普森积分器

    使用逐步加密的辛普森法则，具有更高的收敛阶（O(h⁴)）。
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
        执行自适应辛普森积分

        Args:
            interval: 积分区间
            n_initial_points: 初始点数（可选，必须为偶数）

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
                method="simpson",
                interval=interval,
                config=self.config
            )

        n_points = n_initial_points or max(11, self.config.min_sample_points * 2 + 1)
        n_points = max(n_points, 5)
        if n_points % 2 == 0:
            n_points += 1

        values = []
        prev_result = None

        for iteration in range(self.config.max_iter):
            if time.time() - self._start_time > self._max_time:
                break

            result, evals = self._simpson_rule(a, b, n_points)

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
                        method="simpson",
                        interval=interval,
                        config=self.config
                    )

            prev_result = result
            n_points = n_points * 2 - 1
            if n_points % 2 == 0:
                n_points += 1
            n_points = min(n_points, 100001)

        final_value = values[-1] if values else 0.0
        final_error = abs(values[-1] - values[-2]) / 15.0 if len(values) >= 2 else float('inf')

        return IntegralResult(
            value=final_value,
            error_estimate=final_error,
            intervals_used=n_points,
            function_evaluations=self._total_evaluations,
            iterations=len(values),
            converged=final_error < self.config.tolerance,
            method="simpson",
            interval=interval,
            config=self.config
        )

    def romberg_integrate(
        self,
        interval: Interval,
        n_levels: int = 6
    ) -> IntegralResult:
        """
        龙贝格积分

        使用Romberg外推法进一步提高精度。

        Args:
            interval: 积分区间
            n_levels: 外推层数

        Returns:
            积分结果
        """
        self._total_evaluations = 0
        self._convergence_history = []
        self._start_time = time.time()

        if not interval.is_finite:
            return self._integrate_infinite(interval)

        a, b = interval.start, interval.end

        trapezoidal_values = []
        n_points = 4

        for level in range(n_levels):
            if time.time() - self._start_time > self._max_time:
                break

            result, evals = self._trapezoidal_rule(a, b, n_points)
            trapezoidal_values.append(result)
            self._total_evaluations += evals
            self._convergence_history.append(result)
            n_points = (n_points - 1) * 2 + 1

        romberg_table = self._romberg_extrapolation(trapezoidal_values)

        final_value = romberg_table[-1][-1] if romberg_table else 0.0
        if len(romberg_table) >= 2 and len(romberg_table[-1]) >= 2:
            error = abs(romberg_table[-1][-1] - romberg_table[-2][-2])
        else:
            error = float('inf')

        return IntegralResult(
            value=final_value,
            error_estimate=error,
            intervals_used=n_points,
            function_evaluations=self._total_evaluations,
            iterations=n_levels,
            converged=error < self.config.tolerance,
            method="romberg",
            interval=interval,
            config=self.config
        )

    def _romberg_extrapolation(
        self,
        trapezoidal_values: List[float]
    ) -> List[List[float]]:
        """
        Romberg外推

        Args:
            trapezoidal_values: 梯形法计算值序列

        Returns:
            Romberg积分表
        """
        n = len(trapezoidal_values)
        if n == 0:
            return []

        romberg_table = [[0.0] * n for _ in range(n)]

        for i in range(n):
            romberg_table[i][0] = trapezoidal_values[i]

        for j in range(1, n):
            for i in range(j, n):
                factor = 4 ** j
                denom = factor - 1
                if abs(denom) < 1e-15:
                    denom = 1.0
                romberg_table[i][j] = (
                    factor * romberg_table[i][j-1] - romberg_table[i-1][j-1]
                ) / denom

        return romberg_table

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

    def _simpson_rule(
        self,
        a: float,
        b: float,
        n_points: int
    ) -> Tuple[float, int]:
        """
        复化辛普森公式

        Args:
            a: 积分下限
            b: 积分上限
            n_points: 采样点数（必须为奇数，对应偶数个区间）

        Returns:
            (积分近似值, 函数评估次数)
        """
        n_points = max(n_points, 5)
        if n_points % 2 == 0:
            n_points += 1

        n_intervals = n_points - 1
        h = (b - a) / n_intervals

        result = self._safe_eval(a) + self._safe_eval(b)
        evals = 2

        for i in range(1, n_intervals):
            x = a + i * h
            weight = 4.0 if i % 2 == 1 else 2.0
            result += weight * self._safe_eval(x)
            evals += 1

        result *= h / 3.0

        return result, evals

    def _trapezoidal_rule(
        self,
        a: float,
        b: float,
        n_points: int
    ) -> Tuple[float, int]:
        """
        复化梯形公式（用于龙贝格积分）

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
