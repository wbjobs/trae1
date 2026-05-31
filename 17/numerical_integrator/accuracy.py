"""
精度校验模块
================================

提供积分结果的精度验证和收敛性判断功能。
"""

from typing import Callable, List, Tuple, Optional
from dataclasses import dataclass
import math

from .core_types import (
    IntegralResult,
    IntegralConfig,
    Interval,
    RichardsonResult,
    AccuracyLevel,
)


@dataclass
class ConvergenceInfo:
    """收敛信息"""
    converged: bool
    absolute_error: float
    relative_error: float
    tolerance: float
    iteration: int
    max_iterations: int
    error_reduction_rate: float
    order_of_convergence: float


class AccuracyValidator:
    """
    精度校验器

    提供多种精度验证方法：
    - 绝对误差验证
    - 相对误差验证
    - Richardson外推验证
    - 收敛阶验证
    """

    def __init__(self, config: Optional[IntegralConfig] = None):
        self.config = config or IntegralConfig()

    def check_convergence(
        self,
        current_value: float,
        previous_value: float,
        iteration: int
    ) -> ConvergenceInfo:
        """
        检查收敛性

        Args:
            current_value: 当前计算值
            previous_value: 上一次计算值
            iteration: 当前迭代次数

        Returns:
            收敛信息
        """
        tolerance = self.config.tolerance
        max_iter = self.config.max_iter

        abs_error = abs(current_value - previous_value)

        if abs(current_value) > 1e-15:
            rel_error = abs_error / abs(current_value)
        else:
            rel_error = float('inf')

        converged = abs_error < tolerance or rel_error < tolerance

        error_reduction = 0.0
        if previous_value != 0:
            error_reduction = abs((current_value - previous_value) / previous_value)

        return ConvergenceInfo(
            converged=converged,
            absolute_error=abs_error,
            relative_error=rel_error,
            tolerance=tolerance,
            iteration=iteration,
            max_iterations=max_iter,
            error_reduction_rate=error_reduction,
            order_of_convergence=0.0
        )

    def richardson_extrapolation(
        self,
        values: List[float],
        steps: List[float],
        order: int = 2
    ) -> RichardsonResult:
        """
        Richardson外推

        通过外推法提高积分精度。

        Args:
            values: 不同步长的积分值
            steps: 对应步长
            order: 误差阶数

        Returns:
            Richardson外推结果
        """
        if len(values) < 2:
            return RichardsonResult(
                extrapolated_value=values[-1] if values else 0.0,
                error_estimate=float('inf'),
                order=order,
                values=values,
                steps=steps
            )

        n = len(values)
        extrapolated = values[-1]

        for k in range(1, n):
            weight = 2 ** (order * k)
            if len(values) >= k + 1 and len(values) >= 2:
                val_new = (weight * values[-1] - values[-2]) / (weight - 1)
                values.append(val_new)
                extrapolated = val_new

        error_estimate = abs(values[-1] - values[-2]) if len(values) >= 2 else float('inf')

        return RichardsonResult(
            extrapolated_value=extrapolated,
            error_estimate=error_estimate,
            order=order,
            values=values,
            steps=steps
        )

    def romberg_extrapolation(
        self,
        trapezoidal_values: List[float]
    ) -> List[List[float]]:
        """
        Romberg外推

        基于梯形法的Romberg积分表。

        Args:
            trapezoidal_values: 梯形法计算值序列

        Returns:
            Romberg积分表（二维列表）
        """
        n = len(trapezoidal_values)
        romberg_table = [[0.0] * n for _ in range(n)]

        for i in range(n):
            romberg_table[i][0] = trapezoidal_values[i]

        for j in range(1, n):
            for i in range(j, n):
                factor = 4 ** j
                romberg_table[i][j] = (
                    factor * romberg_table[i][j-1] - romberg_table[i-1][j-1]
                ) / (factor - 1)

        return romberg_table

    def estimate_error(
        self,
        coarse_result: float,
        fine_result: float,
        method: str = "simpson"
    ) -> float:
        """
        估计积分误差

        使用粗网格和细网格结果的差异来估计误差。

        Args:
            coarse_result: 粗网格结果
            fine_result: 细网格结果
            method: 积分方法

        Returns:
            误差估计值
        """
        error_orders = {
            "trapezoidal": 2,
            "simpson": 4,
            "romberg": 6,
        }

        order = error_orders.get(method, 4)
        factor = 2 ** order

        error = abs(fine_result - coarse_result) / (factor - 1)
        return error

    def estimate_convergence_order(
        self,
        values: List[float]
    ) -> float:
        """
        估计收敛阶数

        Args:
            values: 收敛序列

        Returns:
            估计的收敛阶数
        """
        if len(values) < 3:
            return 0.0

        differences = []
        for i in range(len(values) - 1):
            diff = abs(values[i+1] - values[i])
            if diff > 0:
                differences.append(diff)

        if len(differences) < 2:
            return 0.0

        ratios = []
        for i in range(len(differences) - 1):
            if differences[i+1] > 0:
                ratio = differences[i] / differences[i+1]
                if ratio > 1:
                    ratios.append(math.log2(ratio))

        if not ratios:
            return 0.0

        return sum(ratios) / len(ratios)

    def validate_result(
        self,
        result: IntegralResult,
        reference_value: Optional[float] = None
    ) -> ConvergenceInfo:
        """
        验证积分结果

        Args:
            result: 积分结果
            reference_value: 参考值（如果已知）

        Returns:
            收敛验证信息
        """
        tolerance = self.config.tolerance

        if reference_value is not None:
            abs_error = abs(result.value - reference_value)
            if abs(reference_value) > 1e-15:
                rel_error = abs_error / abs(reference_value)
            else:
                rel_error = float('inf')

            converged = abs_error < tolerance or rel_error < tolerance

            return ConvergenceInfo(
                converged=converged,
                absolute_error=abs_error,
                relative_error=rel_error,
                tolerance=tolerance,
                iteration=result.iterations,
                max_iterations=self.config.max_iter,
                error_reduction_rate=0.0,
                order_of_convergence=0.0
            )

        return ConvergenceInfo(
            converged=result.converged,
            absolute_error=result.error_estimate,
            relative_error=result.relative_error,
            tolerance=tolerance,
            iteration=result.iterations,
            max_iterations=self.config.max_iter,
            error_reduction_rate=0.0,
            order_of_convergence=0.0
        )

    def check_stability(
        self,
        results: List[float]
    ) -> bool:
        """
        检查计算稳定性

        验证结果序列是否稳定收敛。

        Args:
            results: 结果序列

        Returns:
            是否稳定
        """
        if len(results) < 3:
            return True

        for i in range(len(results) - 2):
            d1 = abs(results[i+1] - results[i])
            d2 = abs(results[i+2] - results[i+1])

            if d1 > 0 and d2 > d1 * 10:
                return False

        return True

    def calculate_confidence_interval(
        self,
        result: IntegralResult,
        confidence_level: float = 0.95
    ) -> Tuple[float, float]:
        """
        计算置信区间

        Args:
            result: 积分结果
            confidence_level: 置信水平

        Returns:
            (下界, 上界)
        """
        z_scores = {
            0.90: 1.645,
            0.95: 1.96,
            0.99: 2.576,
            0.999: 3.291,
        }

        z = z_scores.get(confidence_level, 1.96)
        half_width = z * result.error_estimate

        return (result.value - half_width, result.value + half_width)

    def adaptive_tolerance(
        self,
        interval: Interval,
        base_tolerance: Optional[float] = None
    ) -> float:
        """
        计算自适应容差

        根据区间大小调整容差。

        Args:
            interval: 积分区间
            base_tolerance: 基础容差

        Returns:
            自适应容差
        """
        tol = base_tolerance or self.config.tolerance

        if not interval.is_finite:
            return tol

        L = interval.length
        reference_length = 1.0

        if L > reference_length:
            return tol * L / reference_length
        else:
            return tol
