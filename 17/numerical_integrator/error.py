"""
误差分析模块
================================

提供积分误差的分析、估计和可视化功能。
"""

from typing import Callable, List, Tuple, Optional, Dict
from dataclasses import dataclass, field
from collections import defaultdict
import math

from .core_types import IntegralResult, IntegralConfig, Interval, AccuracyLevel


@dataclass
class ErrorBreakdown:
    """误差分解"""
    discretization_error: float = 0.0
    roundoff_error: float = 0.0
    truncation_error: float = 0.0
    total_error: float = 0.0
    error_components: Dict[str, float] = field(default_factory=dict)


@dataclass
class ConvergenceAnalysis:
    """收敛性分析"""
    order_of_convergence: float = 0.0
    asymptotic_error_constant: float = 0.0
    error_reduction_rate: float = 0.0
    is_monotonic: bool = True
    convergence_type: str = "unknown"
    efficiency_index: float = 0.0


class ErrorAnalyzer:
    """
    误差分析器

    提供全面的误差分析功能：
    - 离散化误差分析
    - 舍入误差分析
    - 截断误差分析
    - 收敛阶估计
    - 误差传播分析
    - 稳定性分析
    """

    def __init__(self, config: Optional[IntegralConfig] = None):
        self.config = config or IntegralConfig()
        self._error_history: List[float] = []
        self._value_history: List[float] = []

    def analyze_error(
        self,
        result: IntegralResult,
        interval: Optional[Interval] = None
    ) -> ErrorBreakdown:
        """
        综合误差分析

        Args:
            result: 积分结果
            interval: 积分区间

        Returns:
            误差分解
        """
        disc_error = self._estimate_discretization_error(result)
        round_error = self._estimate_roundoff_error(result)
        trunc_error = self._estimate_truncation_error(result, interval)

        total_error = math.sqrt(
            disc_error ** 2 + round_error ** 2 + trunc_error ** 2
        )

        return ErrorBreakdown(
            discretization_error=disc_error,
            roundoff_error=round_error,
            truncation_error=trunc_error,
            total_error=total_error,
            error_components={
                "discretization": disc_error,
                "roundoff": round_error,
                "truncation": trunc_error,
            }
        )

    def _estimate_discretization_error(
        self,
        result: IntegralResult
    ) -> float:
        """
        估计离散化误差

        Args:
            result: 积分结果

        Returns:
            离散化误差估计
        """
        error_orders = {
            "trapezoidal": 2,
            "simpson": 4,
            "romberg": 6,
        }

        order = error_orders.get(result.method, 4)

        if result.intervals_used > 0 and result.interval.is_finite:
            h = result.interval.length / result.intervals_used
            return result.error_estimate * (h ** order) / (h ** order + 1)

        return result.error_estimate * 0.5

    def _estimate_roundoff_error(
        self,
        result: IntegralResult
    ) -> float:
        """
        估计舍入误差

        Args:
            result: 积分结果

        Returns:
            舍入误差估计
        """
        machine_epsilon = 1e-15
        n = result.function_evaluations

        if n > 0:
            return machine_epsilon * math.sqrt(n) * abs(result.value)

        return machine_epsilon * abs(result.value)

    def _estimate_truncation_error(
        self,
        result: IntegralResult,
        interval: Optional[Interval] = None
    ) -> float:
        """
        估计截断误差

        Args:
            result: 积分结果
            interval: 积分区间

        Returns:
            截断误差估计
        """
        if interval is not None and not interval.is_finite:
            return result.error_estimate * 0.3

        return result.error_estimate * 0.2

    def analyze_convergence(
        self,
        results: List[IntegralResult]
    ) -> ConvergenceAnalysis:
        """
        收敛性分析

        Args:
            results: 一系列积分结果

        Returns:
            收敛性分析结果
        """
        if len(results) < 3:
            return ConvergenceAnalysis()

        errors = [r.error_estimate for r in results]
        values = [r.value for r in results]

        self._error_history.extend(errors)
        self._value_history.extend(values)

        order = self._estimate_convergence_order(errors)
        asymptotic_const = self._estimate_asymptotic_error_constant(errors, order)
        reduction_rate = self._calculate_error_reduction_rate(errors)
        is_monotonic = self._check_monotonic_convergence(errors)
        conv_type = self._classify_convergence_type(errors, order)
        efficiency = self._calculate_efficiency_index(results, errors)

        return ConvergenceAnalysis(
            order_of_convergence=order,
            asymptotic_error_constant=asymptotic_const,
            error_reduction_rate=reduction_rate,
            is_monotonic=is_monotonic,
            convergence_type=conv_type,
            efficiency_index=efficiency
        )

    def _estimate_convergence_order(
        self,
        errors: List[float]
    ) -> float:
        """
        估计收敛阶

        Args:
            errors: 误差序列

        Returns:
            收敛阶估计
        """
        if len(errors) < 3:
            return 0.0

        orders = []
        for i in range(len(errors) - 2):
            e1, e2, e3 = errors[i], errors[i+1], errors[i+2]

            if e1 > 0 and e2 > 0 and e3 > 0:
                if e2 != e1 and e3 != e2:
                    try:
                        ratio1 = e2 / e1
                        ratio2 = e3 / e2

                        if ratio1 > 0 and ratio2 > 0 and ratio1 != ratio2:
                            order = abs(
                                math.log(ratio2) / math.log(ratio1)
                            )
                            if 0 < order < 10:
                                orders.append(order)
                    except (ValueError, ZeroDivisionError):
                        continue

        if orders:
            return sum(orders) / len(orders)

        return 0.0

    def _estimate_asymptotic_error_constant(
        self,
        errors: List[float],
        order: float
    ) -> float:
        """
        估计渐近误差常数

        Args:
            errors: 误差序列
            order: 收敛阶

        Returns:
            渐近误差常数估计
        """
        if len(errors) < 2 or order <= 0:
            return 0.0

        constants = []
        for i in range(len(errors) - 1):
            if errors[i] > 0 and errors[i+1] > 0:
                try:
                    ratio = errors[i+1] / errors[i]
                    if ratio > 0:
                        const = errors[i+1] / (ratio ** order)
                        constants.append(const)
                except (ValueError, ZeroDivisionError):
                    continue

        if constants:
            return sum(constants) / len(constants)

        return 0.0

    def _calculate_error_reduction_rate(
        self,
        errors: List[float]
    ) -> float:
        """
        计算误差减少率

        Args:
            errors: 误差序列

        Returns:
            平均误差减少率
        """
        if len(errors) < 2:
            return 0.0

        rates = []
        for i in range(len(errors) - 1):
            if errors[i] > 0:
                rate = (errors[i] - errors[i+1]) / errors[i]
                rates.append(max(0, rate))

        if rates:
            return sum(rates) / len(rates)

        return 0.0

    def _check_monotonic_convergence(
        self,
        errors: List[float]
    ) -> bool:
        """
        检查是否单调收敛

        Args:
            errors: 误差序列

        Returns:
            是否单调收敛
        """
        for i in range(len(errors) - 1):
            if errors[i+1] > errors[i]:
                return False
        return True

    def _classify_convergence_type(
        self,
        errors: List[float],
        order: float
    ) -> str:
        """
        分类收敛类型

        Args:
            errors: 误差序列
            order: 收敛阶

        Returns:
            收敛类型描述
        """
        if order >= 3:
            return "super_convergent"
        elif order >= 1.5:
            return "quadratic"
        elif order >= 0.8:
            return "linear"
        elif order > 0:
            return "sublinear"
        else:
            return "divergent"

    def _calculate_efficiency_index(
        self,
        results: List[IntegralResult],
        errors: List[float]
    ) -> float:
        """
        计算效率指数

        衡量每单位计算成本的精度提升。

        Args:
            results: 积分结果列表
            errors: 误差序列

        Returns:
            效率指数
        """
        if len(results) < 2 or len(errors) < 2:
            return 0.0

        total_evals = sum(r.function_evaluations for r in results)
        total_error_reduction = errors[0] - errors[-1] if errors[0] > 0 else 0

        if total_evals > 0 and total_error_reduction > 0:
            return total_error_reduction / (total_evals * 1e-6)

        return 0.0

    def estimate_error_propagation(
        self,
        function: Callable,
        interval: Interval,
        method: str = "simpson"
    ) -> Dict[str, float]:
        """
        估计误差传播

        Args:
            function: 被积函数
            interval: 积分区间
            method: 积分方法

        Returns:
            误差传播分析结果
        """
        result = {
            "input_error_sensitivity": 0.0,
            "output_error_amplification": 0.0,
            "condition_number": 0.0,
            "stability_constant": 0.0,
        }

        if not interval.is_finite:
            return result

        n_sample = 100
        h = (interval.end - interval.start) / (n_sample - 1)

        f_values = []
        for i in range(n_sample):
            x = interval.start + i * h
            try:
                f_values.append(abs(function(x)))
            except (ValueError, ZeroDivisionError, OverflowError):
                f_values.append(float('inf'))

        max_f = max(f for f in f_values if f != float('inf'))
        avg_f = sum(f for f in f_values if f != float('inf')) / len([f for f in f_values if f != float('inf')])

        result["condition_number"] = interval.length * max_f / max(avg_f, 1e-15)

        error_orders = {
            "trapezoidal": 2,
            "simpson": 4,
            "romberg": 6,
        }

        order = error_orders.get(method, 4)
        result["stability_constant"] = 1.0 / (order * (order + 1))

        if interval.length > 0:
            result["input_error_sensitivity"] = max_f / interval.length

        result["output_error_amplification"] = interval.length * max_f

        return result

    def calculate_error_bounds(
        self,
        result: IntegralResult,
        confidence: float = 0.95
    ) -> Tuple[float, float]:
        """
        计算误差界

        Args:
            result: 积分结果
            confidence: 置信水平

        Returns:
            (下界, 上界)
        """
        error = result.error_estimate

        z_scores = {
            0.90: 1.645,
            0.95: 1.96,
            0.99: 2.576,
            0.999: 3.291,
        }

        z = z_scores.get(confidence, 1.96)
        margin = z * error

        return (result.value - margin, result.value + margin)

    def compare_methods(
        self,
        results: Dict[str, IntegralResult],
        reference_value: Optional[float] = None
    ) -> Dict[str, Dict[str, float]]:
        """
        比较不同方法的结果

        Args:
            results: 方法名到结果的映射
            reference_value: 参考值

        Returns:
            各方法的比较指标
        """
        comparison = {}

        for method, result in results.items():
            metrics = {
                "value": result.value,
                "error_estimate": result.error_estimate,
                "function_evaluations": result.function_evaluations,
                "iterations": result.iterations,
                "converged": 1.0 if result.converged else 0.0,
            }

            if reference_value is not None:
                metrics["actual_error"] = abs(result.value - reference_value)
                if abs(reference_value) > 1e-15:
                    metrics["relative_error"] = metrics["actual_error"] / abs(reference_value)

            if result.function_evaluations > 0:
                metrics["efficiency"] = 1.0 / (result.error_estimate * result.function_evaluations)

            comparison[method] = metrics

        return comparison

    def get_error_summary(
        self,
        result: IntegralResult
    ) -> Dict[str, any]:
        """
        获取误差摘要

        Args:
            result: 积分结果

        Returns:
            误差摘要字典
        """
        breakdown = self.analyze_error(result)

        return {
            "total_error": result.error_estimate,
            "relative_error": result.relative_error,
            "absolute_error": result.error_estimate,
            "accuracy_digits": result.accuracy_digits,
            "error_breakdown": {
                "discretization": breakdown.discretization_error,
                "roundoff": breakdown.roundoff_error,
                "truncation": breakdown.truncation_error,
            },
            "convergence": {
                "converged": result.converged,
                "iterations": result.iterations,
                "max_iterations": self.config.max_iter,
            },
            "computational_cost": {
                "function_evaluations": result.function_evaluations,
                "intervals_used": result.intervals_used,
            },
        }
