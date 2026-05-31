"""
趋势拟合和耗时统计模块
================================

提供积分结果的趋势分析和计算性能统计：
- 收敛趋势分析
- 误差趋势预测
- 计算耗时统计
- 性能评估报告
"""

from typing import Callable, List, Tuple, Optional, Dict
from dataclasses import dataclass, field
from enum import Enum
import math
import time

from .core_types import IntegralResult, IntegralConfig, Interval


@dataclass
class ConvergencePoint:
    """收敛数据点"""
    iteration: int
    value: float
    error: float
    elapsed_time: float
    evaluations: int


@dataclass
class TrendAnalysis:
    """趋势分析结果"""
    is_converging: bool
    convergence_rate: float
    predicted_final_value: float
    predicted_iterations: int
    trend_direction: str
    stability_index: float
    oscillation_count: int


@dataclass
class PerformanceStats:
    """性能统计"""
    total_time: float
    setup_time: float
    computation_time: float
    average_iteration_time: float
    total_evaluations: int
    evaluations_per_second: float
    time_per_evaluation: float
    memory_estimate: float


@dataclass
class IntegrationReport:
    """积分报告"""
    result: IntegralResult
    performance: PerformanceStats
    trend_analysis: TrendAnalysis
    recommendations: List[str]
    warnings: List[str]


class TrendFitter:
    """
    趋势拟合器

    分析积分收敛过程，预测收敛趋势。
    """

    def __init__(self):
        self._convergence_points: List[ConvergencePoint] = []

    def add_point(
        self,
        iteration: int,
        value: float,
        error: float,
        elapsed_time: float,
        evaluations: int
    ):
        """
        添加收敛数据点

        Args:
            iteration: 迭代次数
            value: 当前积分值
            error: 当前误差估计
            elapsed_time: 已用时间
            evaluations: 函数评估次数
        """
        point = ConvergencePoint(
            iteration=iteration,
            value=value,
            error=error,
            elapsed_time=elapsed_time,
            evaluations=evaluations
        )
        self._convergence_points.append(point)

    def analyze(self) -> TrendAnalysis:
        """
        分析收敛趋势

        Returns:
            趋势分析结果
        """
        if len(self._convergence_points) < 2:
            return TrendAnalysis(
                is_converging=True,
                convergence_rate=0.0,
                predicted_final_value=self._convergence_points[-1].value if self._convergence_points else 0.0,
                predicted_iterations=0,
                trend_direction="insufficient_data",
                stability_index=1.0,
                oscillation_count=0
            )

        points = self._convergence_points
        values = [p.value for p in points]
        errors = [p.error for p in points]

        is_converging = self._check_convergence(errors)
        convergence_rate = self._calculate_convergence_rate(errors)
        predicted_value = self._predict_final_value(values, errors)
        predicted_iterations = self._predict_iterations(errors, convergence_rate)
        direction = self._get_trend_direction(values)
        stability = self._calculate_stability(values)
        oscillations = self._count_oscillations(values)

        return TrendAnalysis(
            is_converging=is_converging,
            convergence_rate=convergence_rate,
            predicted_final_value=predicted_value,
            predicted_iterations=predicted_iterations,
            trend_direction=direction,
            stability_index=stability,
            oscillation_count=oscillations
        )

    def fit_error_model(self) -> Tuple[float, float, float]:
        """
        拟合误差衰减模型 error = a * exp(b * iteration) + c

        Returns:
            (a, b, c) 模型参数
        """
        if len(self._convergence_points) < 3:
            return 1.0, -1.0, 0.0

        errors = [p.error for p in self._convergence_points]
        iterations = [p.iteration for p in self._convergence_points]

        positive_errors = [(i, e) for i, e in zip(iterations, errors) if e > 0]
        if len(positive_errors) < 3:
            return 1.0, -1.0, 0.0

        log_errors = [math.log(e + 1e-15) for _, e in positive_errors]
        iters = [i for i, _ in positive_errors]

        n = len(iters)
        sum_x = sum(iters)
        sum_y = sum(log_errors)
        sum_xy = sum(x * y for x, y in zip(iters, log_errors))
        sum_x2 = sum(x * x for x in iters)

        denom = n * sum_x2 - sum_x * sum_x
        if abs(denom) < 1e-15:
            return 1.0, -1.0, 0.0

        b = (n * sum_xy - sum_x * sum_y) / denom
        a_lnx = (sum_y - b * sum_x) / n
        a = math.exp(a_lnx) if a_lnx < 500 else 1e100

        return a, b, 0.0

    def get_points(self) -> List[ConvergencePoint]:
        """获取所有收敛数据点"""
        return self._convergence_points.copy()

    def clear(self):
        """清除数据"""
        self._convergence_points.clear()

    def _check_convergence(self, errors: List[float]) -> bool:
        """检查是否在收敛"""
        if len(errors) < 2:
            return True

        decreasing = sum(1 for i in range(1, len(errors)) if errors[i] <= errors[i-1])
        return decreasing / (len(errors) - 1) > 0.5

    def _calculate_convergence_rate(self, errors: List[float]) -> float:
        """计算收敛速率"""
        if len(errors) < 2 or errors[-1] <= 0:
            return 0.0

        if errors[0] <= 0:
            return 1.0

        ratio = errors[-1] / errors[0]
        if ratio > 0:
            return -math.log(ratio) / len(errors)
        return 0.0

    def _predict_final_value(
        self,
        values: List[float],
        errors: List[float]
    ) -> float:
        """预测最终收敛值"""
        if len(values) < 2:
            return values[-1] if values else 0.0

        if len(values) >= 3 and errors[-1] > 0 and errors[-2] > 0:
            v1, v2, v3 = values[-3], values[-2], values[-1]
            e1, e2 = errors[-2], errors[-1]

            if abs(e2) < 1e-15 or abs(e1 - e2) < 1e-15:
                return values[-1]

            weight = e1 / (e1 - e2) if abs(e1 - e2) > 1e-15 else 1.0
            predicted = v3 + (v3 - v2) * weight
            if abs(predicted - v3) < abs(v3) * 10:
                return predicted

        return values[-1]

    def _predict_iterations(
        self,
        errors: List[float],
        convergence_rate: float
    ) -> int:
        """预测还需要的迭代次数"""
        if len(errors) < 1 or convergence_rate <= 0:
            return 0

        current_error = errors[-1]
        if current_error <= 0:
            return 0

        target_error = 1e-15
        remaining = math.log(current_error / target_error) / convergence_rate
        return max(0, int(math.ceil(remaining)))

    def _get_trend_direction(self, values: List[float]) -> str:
        """获取趋势方向"""
        if len(values) < 2:
            return "stable"

        changes = [values[i] - values[i-1] for i in range(1, len(values))]
        increasing = sum(1 for c in changes if c > 0)
        decreasing = sum(1 for c in changes if c < 0)

        if increasing > len(changes) * 0.7:
            return "increasing"
        elif decreasing > len(changes) * 0.7:
            return "decreasing"
        else:
            return "oscillating"

    def _calculate_stability(self, values: List[float]) -> float:
        """计算稳定性指数"""
        if len(values) < 2:
            return 1.0

        mean_val = sum(values) / len(values)
        if abs(mean_val) < 1e-15:
            return 1.0

        std_val = math.sqrt(sum((v - mean_val) ** 2 for v in values) / len(values))
        cv = std_val / abs(mean_val)
        return 1.0 / (1.0 + cv)

    def _count_oscillations(self, values: List[float]) -> int:
        """计算振荡次数"""
        if len(values) < 3:
            return 0

        count = 0
        for i in range(1, len(values) - 1):
            if (values[i] - values[i-1]) * (values[i+1] - values[i]) < 0:
                count += 1
        return count


class PerformanceTracker:
    """
    性能追踪器

    统计计算耗时和性能指标。
    """

    def __init__(self):
        self._start_time: float = 0.0
        self._setup_start: float = 0.0
        self._setup_end: float = 0.0
        self._computation_start: float = 0.0
        self._end_time: float = 0.0
        self._total_evaluations: int = 0

    def start(self):
        """开始计时"""
        self._start_time = time.time()
        self._setup_start = self._start_time

    def finish_setup(self):
        """完成设置阶段"""
        self._setup_end = time.time()
        self._computation_start = self._setup_end

    def add_evaluations(self, count: int):
        """添加函数评估次数"""
        self._total_evaluations += count

    def stop(self):
        """停止计时"""
        self._end_time = time.time()

    def get_stats(self) -> PerformanceStats:
        """
        获取性能统计

        Returns:
            性能统计数据
        """
        total_time = self._end_time - self._start_time if self._end_time > 0 else time.time() - self._start_time
        setup_time = self._setup_end - self._setup_start if self._setup_end > 0 else 0.0
        computation_time = total_time - setup_time

        avg_iteration_time = computation_time / max(1, 10)
        evals_per_second = self._total_evaluations / total_time if total_time > 0 else 0.0
        time_per_eval = total_time / max(1, self._total_evaluations)

        memory_estimate = self._total_evaluations * 64 / (1024 * 1024)

        return PerformanceStats(
            total_time=total_time,
            setup_time=setup_time,
            computation_time=computation_time,
            average_iteration_time=avg_iteration_time,
            total_evaluations=self._total_evaluations,
            evaluations_per_second=evals_per_second,
            time_per_evaluation=time_per_eval,
            memory_estimate=memory_estimate
        )

    def get_elapsed_time(self) -> float:
        """获取已用时间"""
        return time.time() - self._start_time

    def reset(self):
        """重置计时器"""
        self._start_time = 0.0
        self._setup_start = 0.0
        self._setup_end = 0.0
        self._computation_start = 0.0
        self._end_time = 0.0
        self._total_evaluations = 0
