"""
反常积分处理模块
================================

处理各种类型的反常积分：
- 无穷区间积分
- 瑕积分（含奇点的积分）
- 混合型反常积分
"""

from typing import Callable, List, Tuple, Optional
from dataclasses import dataclass
import math

from .core_types import (
    IntegralConfig,
    IntegralResult,
    Interval,
    IntegralType,
    AccuracyLevel,
)
from .trapezoidal import AdaptiveTrapezoidal
from .simpson import AdaptiveSimpson
from .interval import IntervalSplitter


@dataclass
class ImproperInfo:
    """反常积分信息"""
    integral_type: IntegralType
    singular_points: List[float]
    transformation_method: str
    convergence_type: str


class ImproperIntegral:
    """
    反常积分处理器

    支持处理：
    - 无穷区间积分（使用tan变换）
    - 瑕积分（端点或内部奇点）
    - 混合型反常积分
    """

    def __init__(
        self,
        function: Callable,
        config: Optional[IntegralConfig] = None
    ):
        self.function = function
        self.config = config or IntegralConfig()
        self.splitter = IntervalSplitter(function, self.config)

    def integrate(
        self,
        interval: Interval
    ) -> IntegralResult:
        """
        计算反常积分

        Args:
            interval: 积分区间（可能是无穷区间或包含奇点）

        Returns:
            积分结果
        """
        if not interval.is_finite:
            return self._handle_infinite_interval(interval)

        if interval.singular_points:
            return self._handle_singular_points(interval)

        return self._handle_regular_interval(interval)

    def _handle_infinite_interval(
        self,
        interval: Interval
    ) -> IntegralResult:
        """
        处理无穷区间积分

        使用tan变换将无穷区间转换为有限区间。
        """
        f = self.function

        if interval.is_infinite_start and interval.is_infinite_end:
            def transformed(t: float) -> float:
                if abs(t) >= 1:
                    return 0.0
                x = math.tan(math.pi * t / 2)
                dx_dt = math.pi / (2 * math.cos(math.pi * t / 2) ** 2)
                try:
                    val = f(x) * dx_dt
                    if math.isnan(val) or math.isinf(val):
                        return 0.0
                    return val
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
                    val = f(x) * dx_dt
                    if math.isnan(val) or math.isinf(val):
                        return 0.0
                    return val
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
                    val = f(x) * dx_dt
                    if math.isnan(val) or math.isinf(val):
                        return 0.0
                    return val
                except (ValueError, ZeroDivisionError, OverflowError):
                    return 0.0

            transformed_interval = Interval(start=0.0, end=0.9999)

        return self._integrate_transformed(transformed, transformed_interval)

    def _integrate_transformed(
        self,
        transformed_function: Callable,
        interval: Interval
    ) -> IntegralResult:
        """
        积分变换后的函数

        Args:
            transformed_function: 变换后的函数
            interval: 有限积分区间

        Returns:
            积分结果
        """
        method = self.config.method.lower()

        if method == "trapezoidal":
            integrator = AdaptiveTrapezoidal(transformed_function, self.config)
        else:
            integrator = AdaptiveSimpson(transformed_function, self.config)

        result = integrator.integrate(interval)
        result.method = f"improper_{result.method}"

        return result

    def _handle_singular_points(
        self,
        interval: Interval
    ) -> IntegralResult:
        """
        处理含奇点的积分

        直接对整个区间应用变量变换，避免拆分后的数值不稳定问题。
        """
        singular_point = interval.singular_points[0]
        
        if abs(interval.start - singular_point) < 1e-10:
            return self._handle_left_singularity_full(interval, singular_point)
        elif abs(interval.end - singular_point) < 1e-10:
            return self._handle_right_singularity_full(interval, singular_point)
        else:
            return self._handle_internal_singularity(interval, singular_point)

    def _handle_left_singularity_full(
        self,
        interval: Interval,
        singular_point: float
    ) -> IntegralResult:
        """
        处理左端点奇点 - 对整个区间应用变换

        使用变量替换：x = a + (b-a)*t²
        """
        a = singular_point
        b = interval.end
        f = self.function

        def transformed(t: float) -> float:
            if t <= 0:
                return 2.0
            if t >= 1:
                return 0.0
            x = a + (b - a) * (t ** 2)
            dx_dt = 2 * (b - a) * t
            try:
                val = f(x) * dx_dt
                if math.isnan(val) or math.isinf(val):
                    return 0.0
                return val
            except (ValueError, ZeroDivisionError, OverflowError):
                return 0.0

        transformed_interval = Interval(start=0.0, end=1.0)
        
        method = self.config.method.lower()
        if method == "trapezoidal":
            integrator = AdaptiveTrapezoidal(transformed, self.config)
        else:
            integrator = AdaptiveSimpson(transformed, self.config)
        
        result = integrator.integrate(transformed_interval, n_initial_points=101)
        result.method = f"improper_singular_{result.method}"
        
        return result

    def _handle_right_singularity_full(
        self,
        interval: Interval,
        singular_point: float
    ) -> IntegralResult:
        """
        处理右端点奇点 - 对整个区间应用变换

        使用变量替换：x = b - (b-a)*t²
        """
        a = interval.start
        b = singular_point
        f = self.function

        def transformed(t: float) -> float:
            if t <= 0:
                return 0.0
            if t >= 1:
                return 2.0
            x = b - (b - a) * (t ** 2)
            dx_dt = 2 * (b - a) * t
            try:
                val = f(x) * dx_dt
                if math.isnan(val) or math.isinf(val):
                    return 0.0
                return val
            except (ValueError, ZeroDivisionError, OverflowError):
                return 0.0

        transformed_interval = Interval(start=0.0, end=1.0)
        
        method = self.config.method.lower()
        if method == "trapezoidal":
            integrator = AdaptiveTrapezoidal(transformed, self.config)
        else:
            integrator = AdaptiveSimpson(transformed, self.config)
        
        result = integrator.integrate(transformed_interval, n_initial_points=101)
        result.method = f"improper_singular_{result.method}"
        
        return result

    def _handle_singular_subinterval(
        self,
        interval: Interval
    ) -> IntegralResult:
        """
        处理含奇点的子区间

        使用变量替换处理奇点。
        """
        if not interval.singular_points:
            return self._handle_regular_interval(interval)

        singular_point = interval.singular_points[0]

        if abs(interval.start - singular_point) < 1e-10:
            return self._handle_left_singularity(interval, singular_point)
        elif abs(interval.end - singular_point) < 1e-10:
            return self._handle_right_singularity(interval, singular_point)
        else:
            return self._handle_internal_singularity(interval, singular_point)

    def _handle_left_singularity(
        self,
        interval: Interval,
        singular_point: float
    ) -> IntegralResult:
        """
        处理左端点奇点

        使用变量替换：x = a + (b-a)*t²
        变换后的被积函数在t=0处的极限值是有限的
        """
        a = singular_point
        b = interval.end
        f = self.function

        def transformed(t: float) -> float:
            if t <= 0:
                x = a + 1e-15
                try:
                    return 2 * math.sqrt(x - a) * f(x) if x > a else 0.0
                except:
                    return 0.0
            if t >= 1:
                return 0.0
            x = a + (b - a) * (t ** 2)
            dx_dt = 2 * (b - a) * t
            try:
                val = f(x) * dx_dt
                if math.isnan(val) or math.isinf(val):
                    return 0.0
                return val
            except (ValueError, ZeroDivisionError, OverflowError):
                return 0.0

        transformed_interval = Interval(start=0.0, end=1.0)
        return self._integrate_transformed(transformed, transformed_interval)

    def _handle_right_singularity(
        self,
        interval: Interval,
        singular_point: float
    ) -> IntegralResult:
        """
        处理右端点奇点

        使用变量替换：x = b - (b-a)*t²
        """
        a = interval.start
        b = singular_point
        f = self.function

        def transformed(t: float) -> float:
            if t <= 0:
                return 0.0
            if t >= 1:
                x = b - 1e-15
                try:
                    return 2 * math.sqrt(b - x) * f(x) if x < b else 0.0
                except:
                    return 0.0
            x = b - (b - a) * (t ** 2)
            dx_dt = 2 * (b - a) * t
            try:
                val = f(x) * dx_dt
                if math.isnan(val) or math.isinf(val):
                    return 0.0
                return val
            except (ValueError, ZeroDivisionError, OverflowError):
                return 0.0

        transformed_interval = Interval(start=0.0, end=1.0)
        return self._integrate_transformed(transformed, transformed_interval)

    def _handle_internal_singularity(
        self,
        interval: Interval,
        singular_point: float
    ) -> IntegralResult:
        """
        处理内部奇点

        将区间在奇点处拆分，分别处理。
        """
        left_interval = Interval(
            start=interval.start,
            end=singular_point,
            singular_points=[singular_point]
        )
        right_interval = Interval(
            start=singular_point,
            end=interval.end,
            singular_points=[singular_point]
        )

        left_result = self._handle_right_singularity(left_interval, singular_point)
        right_result = self._handle_left_singularity(right_interval, singular_point)

        left_val = left_result.value if not math.isnan(left_result.value) else 0.0
        right_val = right_result.value if not math.isnan(right_result.value) else 0.0

        return IntegralResult(
            value=left_val + right_val,
            error_estimate=left_result.error_estimate + right_result.error_estimate,
            intervals_used=2,
            function_evaluations=left_result.function_evaluations + right_result.function_evaluations,
            iterations=max(left_result.iterations, right_result.iterations),
            converged=left_result.converged and right_result.converged,
            method=f"improper_internal_singular_{self.config.method}",
            interval=interval,
            config=self.config,
            sub_results=[left_result, right_result]
        )

    def _handle_regular_interval(
        self,
        interval: Interval
    ) -> IntegralResult:
        """
        处理正则区间积分
        """
        method = self.config.method.lower()

        if method == "trapezoidal":
            integrator = AdaptiveTrapezoidal(self.function, self.config)
        else:
            integrator = AdaptiveSimpson(self.function, self.config)

        return integrator.integrate(interval)

    def _estimate_singularity_strength(
        self,
        interval: Interval,
        singular_point: float
    ) -> float:
        """
        估计奇点强度

        通过采样估计函数在奇点附近的行为。
        """
        f = self.function
        epsilon = 1e-8

        try:
            x1 = singular_point + epsilon
            x2 = singular_point + epsilon * 10
            x3 = singular_point + epsilon * 100

            f1 = abs(f(x1)) if x1 < interval.end else 0.0
            f2 = abs(f(x2)) if x2 < interval.end else 0.0
            f3 = abs(f(x3)) if x3 < interval.end else 0.0

            if f1 > 0 and f2 > 0:
                ratio1 = f2 / f1
                if ratio1 > 0:
                    p1 = math.log(ratio1) / math.log(10)
                    return max(0.1, min(0.9, abs(p1)))

        except (ValueError, ZeroDivisionError, OverflowError):
            pass

        return 0.5

    def check_convergence(
        self,
        interval: Interval
    ) -> ImproperInfo:
        """
        检查反常积分的收敛性

        Args:
            interval: 积分区间

        Returns:
            收敛性信息
        """
        if not interval.is_finite:
            integral_type = IntegralType.IMPROPER_INFINITE
        elif interval.singular_points:
            integral_type = IntegralType.IMPROPER_SINGULAR
        else:
            integral_type = IntegralType.DEFINITE

        singular_points = interval.singular_points.copy()

        if interval.is_infinite_start or interval.is_infinite_end:
            transformation = "tan_substitution"
        elif singular_points:
            transformation = "singularity_removal"
        else:
            transformation = "none"

        return ImproperInfo(
            integral_type=integral_type,
            singular_points=singular_points,
            transformation_method=transformation,
            convergence_type="conditional"
        )
