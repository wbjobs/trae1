"""
自适应步长计算模块
================================

提供自适应步长选择策略，根据函数特性和积分方法动态调整步长。
支持智能步长迭代和收敛预测。
"""

from typing import Callable, List, Tuple, Optional
from dataclasses import dataclass
import math

from .core_types import Interval, IntegralConfig, AccuracyLevel, FunctionType


@dataclass
class StepInfo:
    """步长信息"""
    h: float
    n_points: int
    method: str
    error_order: int
    recommended_points: int


@dataclass
class AdaptiveStrategy:
    """自适应策略"""
    initial_points: int
    growth_factor: float
    max_points: int
    convergence_threshold: float
    use_richardson: bool
    error_estimation_method: str


class AdaptiveStepCalculator:
    """
    自适应步长计算器

    根据函数特性和积分方法选择最优步长：
    - 梯形法：误差为O(h²)
    - 辛普森法：误差为O(h⁴)
    - 龙贝格法：误差为O(h²ⁿ)

    支持智能步长调整策略：
    - 保守型：缓慢增加点数，确保稳定性
    - 平衡型：平衡速度和精度
    - 激进型：快速增加点数，追求快速收敛
    """

    def __init__(
        self,
        function: Optional[Callable] = None,
        config: Optional[IntegralConfig] = None,
        strategy: str = "balanced"
    ):
        self.function = function
        self.config = config or IntegralConfig()
        self.strategy = strategy
        self._iteration_count = 0
        self._error_history: List[float] = []
        self._value_history: List[float] = []

    def calculate_initial_step(
        self,
        interval: Interval,
        method: str = "simpson"
    ) -> StepInfo:
        """
        计算初始步长

        根据函数特性和精度要求选择初始步长。

        Args:
            interval: 积分区间
            method: 积分方法 (trapezoidal, simpson, romberg)

        Returns:
            步长信息
        """
        self._iteration_count = 0
        self._error_history.clear()
        self._value_history.clear()

        error_orders = {
            "trapezoidal": 2,
            "simpson": 4,
            "romberg": 6,
        }

        error_order = error_orders.get(method, 4)

        if not interval.is_finite:
            n_points = self.config.min_sample_points
            return StepInfo(
                h=float('inf'),
                n_points=n_points,
                method=method,
                error_order=error_order,
                recommended_points=n_points
            )

        L = interval.end - interval.start
        tolerance = self.config.tolerance

        if self.function is None:
            n_points = max(10, int(L / math.sqrt(tolerance)))
            n_points = max(n_points, self.config.min_sample_points)
            if method == "simpson":
                n_points = max(n_points, 5)
                if n_points % 2 == 0:
                    n_points += 1
            return StepInfo(
                h=L / (n_points - 1),
                n_points=n_points,
                method=method,
                error_order=error_order,
                recommended_points=n_points
            )

        try:
            n_points = self._estimate_points_by_derivative(
                interval, tolerance, error_order
            )
        except (ValueError, ZeroDivisionError, OverflowError):
            n_points = self.config.min_sample_points

        n_points = self._apply_strategy(n_points, method)

        h = L / (n_points - 1) if n_points > 1 else L

        return StepInfo(
            h=h,
            n_points=n_points,
            method=method,
            error_order=error_order,
            recommended_points=n_points
        )

    def get_adaptive_strategy(self) -> AdaptiveStrategy:
        """
        获取自适应策略参数

        Returns:
            自适应策略配置
        """
        strategies = {
            "conservative": AdaptiveStrategy(
                initial_points=100,
                growth_factor=1.5,
                max_points=50000,
                convergence_threshold=0.01,
                use_richardson=True,
                error_estimation_method="richardson"
            ),
            "balanced": AdaptiveStrategy(
                initial_points=50,
                growth_factor=2.0,
                max_points=100000,
                convergence_threshold=0.05,
                use_richardson=True,
                error_estimation_method="step_halving"
            ),
            "aggressive": AdaptiveStrategy(
                initial_points=20,
                growth_factor=3.0,
                max_points=200000,
                convergence_threshold=0.1,
                use_richardson=False,
                error_estimation_method="simple"
            ),
        }

        return strategies.get(self.strategy, strategies["balanced"])

    def recommend_next_points(
        self,
        current_points: int,
        current_error: float,
        target_error: float,
        method: str = "simpson"
    ) -> int:
        """
        推荐下一次迭代的点数

        使用智能步长调整策略，根据收敛历史动态调整。

        Args:
            current_points: 当前点数
            current_error: 当前误差
            target_error: 目标误差
            method: 积分方法

        Returns:
            推荐的新点数
        """
        self._iteration_count += 1
        self._error_history.append(current_error)

        strategy = self.get_adaptive_strategy()

        if current_error <= target_error:
            return current_points

        error_orders = {
            "trapezoidal": 2,
            "simpson": 4,
            "romberg": 6,
        }

        error_order = error_orders.get(method, 4)

        if len(self._error_history) >= 2:
            error_ratio = self._error_history[-1] / self._error_history[-2] if self._error_history[-2] > 0 else 1.0

            if error_ratio > 0.9:
                growth = strategy.growth_factor * 1.5
            elif error_ratio > 0.5:
                growth = strategy.growth_factor
            elif error_ratio > 0.1:
                growth = strategy.growth_factor * 0.7
            else:
                growth = strategy.growth_factor * 0.5
        else:
            error_ratio = current_error / target_error
            if error_ratio > 100:
                growth = strategy.growth_factor * 2
            elif error_ratio > 10:
                growth = strategy.growth_factor * 1.5
            else:
                growth = strategy.growth_factor

        new_points = int(current_points * growth)
        new_points = max(new_points, current_points + 10)
        new_points = min(new_points, strategy.max_points)

        if method == "simpson":
            if new_points % 2 == 0:
                new_points += 1

        return new_points

    def predict_convergence(
        self,
        target_error: float
    ) -> Tuple[int, float]:
        """
        预测收敛所需的迭代次数和最终值

        Returns:
            (预测迭代次数, 预测最终值)
        """
        if len(self._error_history) < 2:
            return 10, 0.0

        recent_errors = self._error_history[-5:] if len(self._error_history) >= 5 else self._error_history

        if len(recent_errors) < 2:
            return 10, 0.0

        avg_rate = 0.0
        for i in range(1, len(recent_errors)):
            if recent_errors[i] > 0 and recent_errors[i-1] > 0:
                rate = recent_errors[i] / recent_errors[i-1]
                avg_rate += rate

        avg_rate /= (len(recent_errors) - 1)

        if avg_rate >= 1.0 or avg_rate <= 0:
            return 50, 0.0

        current_error = recent_errors[-1]
        if current_error <= 0:
            return 0, 0.0

        predicted_iterations = math.log(target_error / current_error) / math.log(avg_rate)
        predicted_iterations = max(1, min(100, int(math.ceil(predicted_iterations))))

        return predicted_iterations, 0.0

    def _apply_strategy(self, n_points: int, method: str) -> int:
        """应用策略调整点数"""
        strategy = self.get_adaptive_strategy()

        n_points = max(n_points, strategy.initial_points)
        n_points = min(n_points, strategy.max_points)

        if method == "simpson":
            n_points = max(n_points, 5)
            if n_points % 2 == 0:
                n_points += 1

        return n_points

    def _estimate_points_by_derivative(
        self,
        interval: Interval,
        tolerance: float,
        error_order: int
    ) -> int:
        """
        通过导数估计采样点数

        Args:
            interval: 积分区间
            tolerance: 目标容差
            error_order: 误差阶数

        Returns:
            估计的采样点数
        """
        L = interval.end - interval.start
        n_sample = 50

        x_mid = (interval.start + interval.end) / 2
        h_est = L / (n_sample - 1)

        max_derivative = 0.0
        for i in range(n_sample):
            x = interval.start + i * h_est
            try:
                if error_order <= 2:
                    d = self._second_derivative(x, h_est * 0.01)
                else:
                    d = self._fourth_derivative(x, h_est * 0.01)
                max_derivative = max(max_derivative, abs(d))
            except (ValueError, ZeroDivisionError, OverflowError):
                continue

        if max_derivative < 1e-15:
            max_derivative = 1.0

        if error_order <= 2:
            coefficient = 1.0 / 12.0
        else:
            coefficient = 1.0 / 2880.0

        required_h = (
            tolerance / (coefficient * max_derivative * L)
        ) ** (1.0 / error_order)

        n_points = int(L / required_h) + 1
        return min(n_points, 10000)

    def _second_derivative(
        self,
        x: float,
        h: float
    ) -> float:
        """
        计算二阶导数的中心差分近似

        Args:
            x: 计算点
            h: 差分步长

        Returns:
            二阶导数近似值
        """
        return (
            self.function(x + h) - 2 * self.function(x) + self.function(x - h)
        ) / (h * h)

    def _fourth_derivative(
        self,
        x: float,
        h: float
    ) -> float:
        """
        计算四阶导数的中心差分近似

        Args:
            x: 计算点
            h: 差分步长

        Returns:
            四阶导数近似值
        """
        return (
            self.function(x + 2 * h)
            - 4 * self.function(x + h)
            + 6 * self.function(x)
            - 4 * self.function(x - h)
            + self.function(x - 2 * h)
        ) / (h ** 4)

    def recommend_step_refinement(
        self,
        current_error: float,
        target_error: float,
        current_n_points: int,
        method: str = "simpson"
    ) -> int:
        """
        推荐步长细化倍数

        根据当前误差和目标误差，计算需要增加的点数。

        Args:
            current_error: 当前误差估计
            target_error: 目标误差
            current_n_points: 当前点数
            method: 积分方法

        Returns:
            推荐的新点数
        """
        error_orders = {
            "trapezoidal": 2,
            "simpson": 4,
            "romberg": 6,
        }

        error_order = error_orders.get(method, 4)

        if current_error <= target_error:
            return current_n_points

        error_ratio = current_error / target_error
        refinement_factor = error_ratio ** (1.0 / error_order)
        refinement_factor = min(refinement_factor, 10.0)

        new_n_points = int(current_n_points * refinement_factor)
        new_n_points = max(new_n_points, current_n_points + 2)

        if method == "simpson":
            if new_n_points % 2 == 0:
                new_n_points += 1

        return new_n_points

    def calculate_step_sequence(
        self,
        interval: Interval,
        method: str = "simpson",
        n_levels: int = 5
    ) -> List[StepInfo]:
        """
        计算步长序列（用于龙贝格积分和Richardson外推）

        Args:
            interval: 积分区间
            method: 积分方法
            n_levels: 序列长度

        Returns:
            步长信息序列
        """
        steps = []
        base_points = self.calculate_initial_step(interval, method)

        for i in range(n_levels):
            n_points = base_points.n_points * (2 ** i)
            if method == "simpson" and n_points % 2 == 0:
                n_points += 1

            if interval.is_finite:
                h = (interval.end - interval.start) / (n_points - 1)
            else:
                h = float('inf')

            steps.append(StepInfo(
                h=h,
                n_points=n_points,
                method=method,
                error_order=base_points.error_order,
                recommended_points=n_points
            ))

        return steps

    def get_optimal_split_count(
        self,
        interval: Interval,
        error_estimate: float,
        target_error: float
    ) -> int:
        """
        获取最优区间拆分数

        Args:
            interval: 积分区间
            error_estimate: 误差估计
            target_error: 目标误差

        Returns:
            推荐的拆分数
        """
        if error_estimate <= target_error:
            return 1

        ratio = error_estimate / target_error
        n_splits = int(math.ceil(math.sqrt(ratio)))
        n_splits = min(n_splits, 16)
        n_splits = max(n_splits, 2)

        return n_splits

    def estimate_function_complexity(
        self,
        interval: Interval
    ) -> FunctionType:
        """
        估计函数复杂度

        Args:
            interval: 积分区间

        Returns:
            函数类型
        """
        if self.function is None:
            return FunctionType.REGULAR

        if not interval.is_finite:
            return FunctionType.SINGULAR

        n_sample = 100
        h = (interval.end - interval.start) / (n_sample - 1)

        f_values = []
        for i in range(n_sample):
            x = interval.start + i * h
            try:
                f_values.append(self.function(x))
            except (ValueError, ZeroDivisionError, OverflowError):
                return FunctionType.SINGULAR

        if len(f_values) < 3:
            return FunctionType.REGULAR

        second_derivatives = []
        for i in range(1, n_sample - 1):
            d2 = abs(
                f_values[i+1] - 2 * f_values[i] + f_values[i-1]
            ) / (h * h)
            second_derivatives.append(d2)

        avg_d2 = sum(second_derivatives) / len(second_derivatives)
        max_d2 = max(second_derivatives)

        if max_d2 > 1e10:
            return FunctionType.SINGULAR

        if avg_d2 > 1e6:
            return FunctionType.OSCILLATORY

        sign_changes = 0
        for i in range(len(f_values) - 1):
            if f_values[i] * f_values[i+1] < 0:
                sign_changes += 1

        if sign_changes > n_sample * 0.3:
            return FunctionType.OSCILLATORY

        if max_d2 < 100 * avg_d2:
            return FunctionType.SMOOTH

        return FunctionType.REGULAR
