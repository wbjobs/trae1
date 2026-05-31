"""
多重积分处理模块
================================

实现多重积分的计算，支持：
- 二重积分
- 三重积分
- 高维积分
- 矩形区域和一般区域
"""

from typing import Callable, List, Tuple, Optional, Union
from dataclasses import dataclass, field
import math
import time

from .core_types import (
    IntegralConfig,
    IntegralResult,
    Interval,
    MultiDimensionalInterval,
)
from .trapezoidal import AdaptiveTrapezoidal
from .simpson import AdaptiveSimpson


class MultipleIntegral:
    """
    多重积分计算器

    使用迭代积分方法计算多维积分。
    """

    def __init__(
        self,
        function: Callable,
        config: Optional[IntegralConfig] = None
    ):
        self.function = function
        self.config = config or IntegralConfig()
        self._max_time = 10.0

    def integrate(
        self,
        multi_interval: MultiDimensionalInterval
    ) -> IntegralResult:
        """
        计算多重积分

        Args:
            multi_interval: 多维积分区间

        Returns:
            积分结果
        """
        if multi_interval.dimension == 1:
            return self._integrate_1d(multi_interval)
        elif multi_interval.dimension == 2:
            return self._integrate_2d(multi_interval)
        elif multi_interval.dimension == 3:
            return self._integrate_3d(multi_interval)
        else:
            return self._integrate_nd(multi_interval)

    def _integrate_1d(
        self,
        multi_interval: MultiDimensionalInterval
    ) -> IntegralResult:
        """一维积分"""
        interval = multi_interval.intervals[0]
        method = self.config.method.lower()

        if method == "trapezoidal":
            integrator = AdaptiveTrapezoidal(self.function, self.config)
        else:
            integrator = AdaptiveSimpson(self.function, self.config)

        result = integrator.integrate(interval)
        return result

    def _integrate_2d(
        self,
        multi_interval: MultiDimensionalInterval
    ) -> IntegralResult:
        """
        二重积分

        使用固定采样点的梯形法则进行外积分，
        对每个x点计算内积分。
        """
        intervals = multi_interval.intervals
        x_interval = intervals[0]
        y_interval = intervals[1]

        y_limits = None
        if multi_interval.variable_limits and len(multi_interval.variable_limits) >= 2:
            y_limits = multi_interval.variable_limits[1]

        start_time = time.time()

        n_x = max(10, self.config.min_sample_points)
        n_x = min(n_x, 50)

        prev_total = None
        final_result = 0.0
        final_error = 0.0
        total_evals = 0

        for iteration in range(min(self.config.max_iter, 10)):
            if time.time() - start_time > self._max_time:
                break

            if x_interval.is_finite:
                x_values = [
                    x_interval.start + i * (x_interval.end - x_interval.start) / (n_x - 1)
                    for i in range(n_x)
                ]
            else:
                x_values = self._transform_infinite(x_interval, n_x)

            h_x = (x_values[-1] - x_values[0]) / (n_x - 1) if n_x > 1 else 1.0

            current_total = 0.0
            current_error = 0.0

            for i, x in enumerate(x_values):
                if time.time() - start_time > self._max_time:
                    break

                if i == 0 or i == n_x - 1:
                    weight = 0.5
                else:
                    weight = 1.0

                if y_limits is not None:
                    try:
                        y_low, y_high = y_limits(x)
                    except Exception:
                        continue
                    current_y_interval = Interval(start=y_low, end=y_high)
                else:
                    current_y_interval = y_interval

                inner_func = self._make_inner_function(x)
                method = self.config.method.lower()

                if method == "trapezoidal":
                    inner_integrator = AdaptiveTrapezoidal(inner_func, self.config)
                else:
                    inner_integrator = AdaptiveSimpson(inner_func, self.config)

                inner_result = inner_integrator.integrate(current_y_interval)

                current_total += weight * inner_result.value
                current_error += weight * inner_result.error_estimate
                total_evals += inner_result.function_evaluations

            current_total *= h_x
            current_error *= h_x

            if prev_total is not None:
                improvement = abs(current_total - prev_total)
                if improvement < self.config.tolerance * 10:
                    final_result = current_total
                    final_error = current_error
                    break

            prev_total = current_total
            final_result = current_total
            final_error = current_error

            n_x = min(n_x * 2, 100)

        return IntegralResult(
            value=final_result,
            error_estimate=final_error,
            intervals_used=n_x,
            function_evaluations=total_evals,
            iterations=iteration + 1,
            converged=final_error < self.config.tolerance * 10,
            method=f"2d_{self.config.method}",
            interval=x_interval,
            config=self.config
        )

    def _integrate_3d(
        self,
        multi_interval: MultiDimensionalInterval
    ) -> IntegralResult:
        """
        三重积分

        使用固定采样点的迭代方法。
        """
        intervals = multi_interval.intervals
        x_interval = intervals[0]
        y_interval = intervals[1]
        z_interval = intervals[2]

        start_time = time.time()

        n_x = max(8, min(self.config.min_sample_points, 20))
        n_y = max(8, min(self.config.min_sample_points, 20))

        if x_interval.is_finite:
            x_values = [
                x_interval.start + i * (x_interval.end - x_interval.start) / (n_x - 1)
                for i in range(n_x)
            ]
        else:
            x_values = self._transform_infinite(x_interval, n_x)

        if y_interval.is_finite:
            y_values = [
                y_interval.start + j * (y_interval.end - y_interval.start) / (n_y - 1)
                for j in range(n_y)
            ]
        else:
            y_values = self._transform_infinite(y_interval, n_y)

        h_x = (x_values[-1] - x_values[0]) / (n_x - 1) if n_x > 1 else 1.0
        h_y = (y_values[-1] - y_values[0]) / (n_y - 1) if n_y > 1 else 1.0

        total_result = 0.0
        total_error = 0.0
        total_evals = 0

        for i, x in enumerate(x_values):
            if time.time() - start_time > self._max_time:
                break

            weight_x = 0.5 if i == 0 or i == n_x - 1 else 1.0

            for j, y in enumerate(y_values):
                if time.time() - start_time > self._max_time:
                    break

                weight_y = 0.5 if j == 0 or j == n_y - 1 else 1.0

                inner_func = self._make_2d_inner_function(x, y)
                method = self.config.method.lower()

                if method == "trapezoidal":
                    inner_integrator = AdaptiveTrapezoidal(inner_func, self.config)
                else:
                    inner_integrator = AdaptiveSimpson(inner_func, self.config)

                inner_result = inner_integrator.integrate(z_interval)

                weight = weight_x * weight_y
                total_result += weight * inner_result.value
                total_error += weight * inner_result.error_estimate
                total_evals += inner_result.function_evaluations

        total_result *= h_x * h_y
        total_error *= h_x * h_y

        return IntegralResult(
            value=total_result,
            error_estimate=total_error,
            intervals_used=n_x * n_y,
            function_evaluations=total_evals,
            iterations=n_x * n_y,
            converged=total_error < self.config.tolerance * 10,
            method=f"3d_{self.config.method}",
            interval=x_interval,
            config=self.config
        )

    def _integrate_nd(
        self,
        multi_interval: MultiDimensionalInterval
    ) -> IntegralResult:
        """
        N维积分（N > 3）

        使用递归方法计算高维积分。
        """
        return self._integrate_recursive(
            self.function,
            multi_interval.intervals,
            0,
            []
        )

    def _integrate_recursive(
        self,
        function: Callable,
        intervals: List[Interval],
        depth: int,
        args: List[float]
    ) -> IntegralResult:
        """
        递归计算多维积分
        """
        if depth == len(intervals) - 1:
            interval = intervals[depth]

            def inner_func(x):
                all_args = args + [x]
                return function(*all_args)

            method = self.config.method.lower()
            if method == "trapezoidal":
                integrator = AdaptiveTrapezoidal(inner_func, self.config)
            else:
                integrator = AdaptiveSimpson(inner_func, self.config)

            return integrator.integrate(interval)

        interval = intervals[depth]
        n_points = max(5, min(self.config.min_sample_points, 15))

        if interval.is_finite:
            x_values = [
                interval.start + i * (interval.end - interval.start) / (n_points - 1)
                for i in range(n_points)
            ]
        else:
            x_values = self._transform_infinite(interval, n_points)

        h = (x_values[-1] - x_values[0]) / (n_points - 1) if n_points > 1 else 1.0

        total_result = 0.0
        total_error = 0.0
        total_evals = 0

        for i, x in enumerate(x_values):
            weight = 0.5 if i == 0 or i == n_points - 1 else 1.0

            new_args = args + [x]
            sub_result = self._integrate_recursive(
                function, intervals, depth + 1, new_args
            )

            total_result += weight * sub_result.value
            total_error += weight * sub_result.error_estimate
            total_evals += sub_result.function_evaluations

        total_result *= h
        total_error *= h

        return IntegralResult(
            value=total_result,
            error_estimate=total_error,
            intervals_used=n_points,
            function_evaluations=total_evals,
            iterations=depth + 1,
            converged=total_error < self.config.tolerance * 10,
            method=f"{len(intervals)}d_{self.config.method}",
            interval=interval,
            config=self.config
        )

    def _make_inner_function(self, x_val: float) -> Callable:
        """创建固定x的内积分函数"""
        f = self.function
        return lambda y: f(x_val, y)

    def _make_2d_inner_function(self, x_val: float, y_val: float) -> Callable:
        """创建固定x,y的内积分函数"""
        f = self.function
        return lambda z: f(x_val, y_val, z)

    def _transform_infinite(
        self,
        interval: Interval,
        n_points: int
    ) -> List[float]:
        """
        转换无穷区间为有限采样点

        使用更稳定的tan变换。
        """
        if interval.is_infinite_start and interval.is_infinite_end:
            t_values = [
                -0.9 + 1.8 * i / (n_points - 1)
                for i in range(n_points)
            ]
            return [math.tan(math.pi * t / 2) for t in t_values]

        elif interval.is_infinite_start:
            t_values = [
                -0.9 + 0.9 * i / (n_points - 1)
                for i in range(n_points)
            ]
            b = interval.end
            return [b + math.tan(math.pi * t / 2) for t in t_values]

        else:
            t_values = [
                0.0 + 0.9 * i / (n_points - 1)
                for i in range(n_points)
            ]
            a = interval.start
            return [a + math.tan(math.pi * t / 2) for t in t_values]

    def integrate_rectangular(
        self,
        a: float,
        b: float,
        c: float,
        d: float
    ) -> IntegralResult:
        """计算矩形区域上的二重积分"""
        multi_interval = MultiDimensionalInterval(
            intervals=[
                Interval(start=a, end=b),
                Interval(start=c, end=d)
            ]
        )
        return self._integrate_2d(multi_interval)

    def integrate_box(
        self,
        x_range: Tuple[float, float],
        y_range: Tuple[float, float],
        z_range: Tuple[float, float]
    ) -> IntegralResult:
        """计算长方体区域上的三重积分"""
        multi_interval = MultiDimensionalInterval(
            intervals=[
                Interval(start=x_range[0], end=x_range[1]),
                Interval(start=y_range[0], end=y_range[1]),
                Interval(start=z_range[0], end=z_range[1])
            ]
        )
        return self._integrate_3d(multi_interval)

    def integrate_variable_limits(
        self,
        a: float,
        b: float,
        y_lower: Callable,
        y_upper: Callable
    ) -> IntegralResult:
        """计算一般区域上的二重积分"""
        multi_interval = MultiDimensionalInterval(
            intervals=[
                Interval(start=a, end=b),
                Interval(start=0.0, end=1.0)
            ],
            variable_limits=[None, lambda x: (y_lower(x), y_upper(x))]
        )
        return self._integrate_2d(multi_interval)
