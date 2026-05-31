"""
积分区间拆分模块
================================

提供多种区间拆分策略，用于自适应积分中的区间细分。
支持均匀拆分、自适应拆分、基于梯度和曲率的拆分。
"""

from typing import Callable, List, Tuple, Optional
from dataclasses import dataclass
import math

from .core_types import Interval, SplitRule, IntegralConfig


@dataclass
class SplitPoint:
    """拆分点信息"""
    position: float
    priority: float
    reason: str


class IntervalSplitter:
    """
    积分区间拆分器

    提供多种区间拆分策略：
    - UNIFORM: 均匀拆分
    - ADAPTIVE: 基于误差的自适应拆分
    - GRADIENT_BASED: 基于函数梯度的拆分
    - CURVATURE_BASED: 基于曲率的拆分
    - RECURSIVE: 递归二分拆分
    """

    def __init__(
        self,
        function: Optional[Callable] = None,
        config: Optional[IntegralConfig] = None
    ):
        self.function = function
        self.config = config or IntegralConfig()

    def split(
        self,
        interval: Interval,
        rule: Optional[SplitRule] = None,
        n_parts: int = 2
    ) -> List[Interval]:
        """
        根据指定规则拆分区间

        Args:
            interval: 待拆分的区间
            rule: 拆分规则，默认使用配置中的规则
            n_parts: 拆分数量（均匀拆分时使用）

        Returns:
            拆分后的子区间列表
        """
        if not interval.is_finite:
            return [interval]

        rule = rule or self.config.split_rule

        strategies = {
            SplitRule.UNIFORM: self._uniform_split,
            SplitRule.ADAPTIVE: self._adaptive_split,
            SplitRule.GRADIENT_BASED: self._gradient_split,
            SplitRule.CURVATURE_BASED: self._curvature_split,
            SplitRule.RECURSIVE: self._recursive_split,
        }

        strategy = strategies.get(rule, self._adaptive_split)
        return strategy(interval, n_parts)

    def _uniform_split(
        self,
        interval: Interval,
        n_parts: int = 2
    ) -> List[Interval]:
        """
        均匀拆分区间

        将区间等分为n_parts个子区间。

        Args:
            interval: 待拆分的区间
            n_parts: 拆分数量

        Returns:
            均匀拆分后的子区间列表
        """
        if not interval.is_finite or n_parts <= 1:
            return [interval]

        step = (interval.end - interval.start) / n_parts
        result = []

        for i in range(n_parts):
            start = interval.start + i * step
            end = start + step
            sub_singular = [
                s for s in interval.singular_points
                if start <= s <= end
            ]
            result.append(Interval(
                start=start,
                end=end,
                singular_points=sub_singular,
                weight=interval.weight / n_parts
            ))

        return result

    def _adaptive_split(
        self,
        interval: Interval,
        n_parts: int = 2
    ) -> List[Interval]:
        """
        自适应拆分区间

        基于函数值变化进行拆分，在函数变化剧烈的区域增加密度。

        Args:
            interval: 待拆分的区间
            n_parts: 期望的最小拆分数量

        Returns:
            自适应拆分后的子区间列表
        """
        if not interval.is_finite or self.function is None:
            return self._uniform_split(interval, n_parts)

        n_sample = max(100, n_parts * 20)
        x_values = [
            interval.start + i * (interval.end - interval.start) / (n_sample - 1)
            for i in range(n_sample)
        ]

        try:
            f_values = [abs(self.function(x)) for x in x_values]
        except (ValueError, ZeroDivisionError, OverflowError):
            return self._uniform_split(interval, n_parts)

        f_max = max(f_values) if f_values else 1.0
        f_max = max(f_max, 1e-15)

        weights = []
        for i in range(n_sample - 1):
            change = abs(f_values[i+1] - f_values[i])
            avg = (f_values[i] + f_values[i+1]) / 2 + 1e-15
            weights.append(1.0 + change / avg)

        split_points = self._weighted_split_points(
            interval, x_values, weights, n_parts
        )

        return self._create_intervals_from_points(interval, split_points)

    def _gradient_split(
        self,
        interval: Interval,
        n_parts: int = 2
    ) -> List[Interval]:
        """
        基于梯度的区间拆分

        在函数梯度较大的区域进行更密集的拆分。

        Args:
            interval: 待拆分的区间
            n_parts: 期望的最小拆分数量

        Returns:
            基于梯度拆分后的子区间列表
        """
        if not interval.is_finite or self.function is None:
            return self._uniform_split(interval, n_parts)

        n_sample = max(100, n_parts * 20)
        x_values = [
            interval.start + i * (interval.end - interval.start) / (n_sample - 1)
            for i in range(n_sample)
        ]

        try:
            f_values = [self.function(x) for x in x_values]
        except (ValueError, ZeroDivisionError, OverflowError):
            return self._uniform_split(interval, n_parts)

        dx = (interval.end - interval.start) / (n_sample - 1)

        weights = []
        for i in range(n_sample - 1):
            gradient = abs(f_values[i+1] - f_values[i]) / dx
            weights.append(1.0 + gradient)

        if max(weights) == 0:
            return self._uniform_split(interval, n_parts)

        max_weight = max(weights)
        weights = [w / max_weight for w in weights]

        split_points = self._weighted_split_points(
            interval, x_values, weights, n_parts
        )

        return self._create_intervals_from_points(interval, split_points)

    def _curvature_split(
        self,
        interval: Interval,
        n_parts: int = 2
    ) -> List[Interval]:
        """
        基于曲率的区间拆分

        在函数曲率较大的区域进行更密集的拆分。

        Args:
            interval: 待拆分的区间
            n_parts: 期望的最小拆分数量

        Returns:
            基于曲率拆分后的子区间列表
        """
        if not interval.is_finite or self.function is None:
            return self._uniform_split(interval, n_parts)

        n_sample = max(100, n_parts * 20)
        x_values = [
            interval.start + i * (interval.end - interval.start) / (n_sample - 1)
            for i in range(n_sample)
        ]

        try:
            f_values = [self.function(x) for x in x_values]
        except (ValueError, ZeroDivisionError, OverflowError):
            return self._uniform_split(interval, n_parts)

        dx = (interval.end - interval.start) / (n_sample - 1)

        weights = [0.0] * (n_sample - 1)
        for i in range(1, n_sample - 1):
            second_derivative = abs(
                f_values[i+1] - 2 * f_values[i] + f_values[i-1]
            ) / (dx * dx)
            weights[i] = 1.0 + second_derivative
            weights[i-1] = max(weights[i-1], weights[i])

        if max(weights) == 0:
            return self._uniform_split(interval, n_parts)

        max_weight = max(weights)
        weights = [w / max_weight for w in weights]

        split_points = self._weighted_split_points(
            interval, x_values, weights, n_parts
        )

        return self._create_intervals_from_points(interval, split_points)

    def _recursive_split(
        self,
        interval: Interval,
        n_parts: int = 2
    ) -> List[Interval]:
        """
        递归二分拆分

        递归地将区间二分，直到达到期望的拆分数量。

        Args:
            interval: 待拆分的区间
            n_parts: 期望的最小拆分数量

        Returns:
            递归拆分后的子区间列表
        """
        if not interval.is_finite or n_parts <= 1:
            return [interval]

        n_bisections = int(math.ceil(math.log2(n_parts)))
        actual_parts = 2 ** n_bisections

        return self._uniform_split(interval, actual_parts)

    def _weighted_split_points(
        self,
        interval: Interval,
        x_values: List[float],
        weights: List[float],
        n_parts: int
    ) -> List[float]:
        """
        根据权重计算拆分点

        Args:
            interval: 原始区间
            x_values: 采样点位置
            weights: 对应权重
            n_parts: 期望拆分数量

        Returns:
            拆分点位置列表
        """
        total_weight = sum(weights)
        if total_weight == 0:
            return []

        target_weight_per_segment = total_weight / n_parts

        split_points = []
        cumulative = 0.0

        for i in range(len(weights)):
            cumulative += weights[i]
            while cumulative >= target_weight_per_segment * (len(split_points) + 1):
                if len(split_points) >= n_parts - 1:
                    break
                pos = (x_values[i] + x_values[i+1]) / 2
                if pos > interval.start and pos < interval.end:
                    split_points.append(pos)

        return split_points

    def _create_intervals_from_points(
        self,
        interval: Interval,
        split_points: List[float]
    ) -> List[Interval]:
        """
        根据拆分点创建子区间

        Args:
            interval: 原始区间
            split_points: 拆分点位置列表

        Returns:
            子区间列表
        """
        all_points = [interval.start] + sorted(split_points) + [interval.end]
        result = []

        for i in range(len(all_points) - 1):
            start = all_points[i]
            end = all_points[i+1]
            sub_singular = [
                s for s in interval.singular_points
                if start <= s <= end
            ]
            result.append(Interval(
                start=start,
                end=end,
                singular_points=sub_singular,
                weight=interval.weight * (end - start) / interval.length
            ))

        return result

    def refine_interval(
        self,
        interval: Interval,
        error_ratio: float,
        target_error: float
    ) -> List[Interval]:
        """
        根据误差比例细化区间

        当误差较大时，将区间细分为更多子区间。

        Args:
            interval: 待细化的区间
            error_ratio: 当前误差与目标误差的比值
            target_error: 目标误差

        Returns:
            细化后的子区间列表
        """
        if error_ratio <= 1.0:
            return [interval]

        n_subintervals = min(
            int(math.ceil(math.sqrt(error_ratio))),
            16
        )
        n_subintervals = max(n_subintervals, 2)

        return self._uniform_split(interval, n_subintervals)

    def find_singular_regions(
        self,
        interval: Interval,
        threshold: float = 1e10
    ) -> List[Interval]:
        """
        查找区间中的奇异区域

        Args:
            interval: 待检查的区间
            threshold: 判定为奇异的阈值

        Returns:
            包含奇异点的子区间列表
        """
        if not interval.is_finite or self.function is None:
            return []

        singular_regions = []
        n_sample = 1000

        for i in range(n_sample):
            x = interval.start + i * (interval.end - interval.start) / (n_sample - 1)
            try:
                f_val = abs(self.function(x))
                if f_val > threshold:
                    region_start = x - (interval.end - interval.start) / n_sample
                    region_end = x + (interval.end - interval.start) / n_sample
                    region_start = max(region_start, interval.start)
                    region_end = min(region_end, interval.end)

                    if not singular_regions or region_start > singular_regions[-1].end:
                        singular_regions.append(Interval(
                            start=region_start,
                            end=region_end,
                            singular_points=[x]
                        ))
                    else:
                        singular_regions[-1].end = max(
                            singular_regions[-1].end,
                            region_end
                        )
            except (ValueError, ZeroDivisionError, OverflowError):
                region_start = x - (interval.end - interval.start) / n_sample
                region_end = x + (interval.end - interval.start) / n_sample
                singular_regions.append(Interval(
                    start=region_start,
                    end=region_end,
                    singular_points=[x]
                ))

        return singular_regions

    @staticmethod
    def merge_overlapping_intervals(intervals: List[Interval]) -> List[Interval]:
        """
        合并重叠的区间

        Args:
            intervals: 待合并的区间列表

        Returns:
            合并后的区间列表
        """
        if not intervals:
            return []

        sorted_intervals = sorted(intervals, key=lambda iv: iv.start)
        merged = [sorted_intervals[0]]

        for current in sorted_intervals[1:]:
            last = merged[-1]
            if current.start <= last.end:
                last.end = max(last.end, current.end)
                last.singular_points.extend(current.singular_points)
            else:
                merged.append(current)

        return merged

    @staticmethod
    def get_split_points_for_singular(
        interval: Interval,
        singular_points: List[float],
        margin: float = 1e-10
    ) -> List[Interval]:
        """
        为奇异点周围创建子区间

        Args:
            interval: 原始区间
            singular_points: 奇异点位置列表
            margin: 奇异点周围的安全距离

        Returns:
            拆分后的区间列表
        """
        if not singular_points:
            return [interval]

        cut_points = [interval.start]
        for sp in sorted(singular_points):
            if interval.start < sp - margin:
                cut_points.append(sp - margin)
            cut_points.append(sp)
            if sp + margin < interval.end:
                cut_points.append(sp + margin)
        cut_points.append(interval.end)

        cut_points = sorted(set(cut_points))

        result = []
        for i in range(len(cut_points) - 1):
            result.append(Interval(
                start=cut_points[i],
                end=cut_points[i+1],
                singular_points=[
                    sp for sp in singular_points
                    if cut_points[i] <= sp <= cut_points[i+1]
                ]
            ))

        return result
