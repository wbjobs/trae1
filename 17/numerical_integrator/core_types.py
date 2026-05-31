"""
核心数据类型定义模块
================================

定义系统中使用的所有数据结构、枚举和配置类型。
"""

from dataclasses import dataclass, field
from enum import Enum
from typing import Callable, List, Optional, Tuple, Dict, Any
import math


class FunctionType(Enum):
    """被积函数类型"""
    REGULAR = "regular"
    SINGULAR = "singular"
    OSCILLATORY = "oscillatory"
    SMOOTH = "smooth"


class IntegralType(Enum):
    """积分类型"""
    DEFINITE = "definite"
    IMPROPER_INFINITE = "improper_infinite"
    IMPROPER_SINGULAR = "improper_singular"
    MULTIPLE = "multiple"


class AccuracyLevel(Enum):
    """精度等级"""
    LOW = "low"
    MEDIUM = "medium"
    HIGH = "high"
    VERY_HIGH = "very_high"
    ULTRA = "ultra"

    def get_tolerance(self) -> float:
        """获取对应精度等级的容差"""
        tolerances = {
            AccuracyLevel.LOW: 1e-4,
            AccuracyLevel.MEDIUM: 1e-6,
            AccuracyLevel.HIGH: 1e-8,
            AccuracyLevel.VERY_HIGH: 1e-10,
            AccuracyLevel.ULTRA: 1e-12,
        }
        return tolerances[self]

    def get_max_iterations(self) -> int:
        """获取对应精度等级的最大迭代次数"""
        iterations = {
            AccuracyLevel.LOW: 20,
            AccuracyLevel.MEDIUM: 30,
            AccuracyLevel.HIGH: 40,
            AccuracyLevel.VERY_HIGH: 50,
            AccuracyLevel.ULTRA: 60,
        }
        return iterations[self]

    def get_min_points(self) -> int:
        """获取对应精度等级的最小采样点数"""
        points = {
            AccuracyLevel.LOW: 10,
            AccuracyLevel.MEDIUM: 20,
            AccuracyLevel.HIGH: 50,
            AccuracyLevel.VERY_HIGH: 100,
            AccuracyLevel.ULTRA: 200,
        }
        return points[self]


class SplitRule(Enum):
    """区间拆分规则"""
    UNIFORM = "uniform"
    ADAPTIVE = "adaptive"
    GRADIENT_BASED = "gradient_based"
    CURVATURE_BASED = "curvature_based"
    RECURSIVE = "recursive"


@dataclass
class Interval:
    """积分区间"""
    start: float
    end: float
    is_infinite_start: bool = False
    is_infinite_end: bool = False
    singular_points: List[float] = field(default_factory=list)
    weight: float = 1.0

    @property
    def length(self) -> float:
        if self.is_infinite_start or self.is_infinite_end:
            return float('inf')
        return self.end - self.start

    @property
    def is_finite(self) -> bool:
        return not (self.is_infinite_start or self.is_infinite_end)

    def contains(self, x: float) -> bool:
        if self.is_infinite_start and self.is_infinite_end:
            return True
        if self.is_infinite_start:
            return x <= self.end
        if self.is_infinite_end:
            return x >= self.start
        return self.start <= x <= self.end

    def __repr__(self) -> str:
        left = "(-∞" if self.is_infinite_start else f"[{self.start}"
        right = "+∞)" if self.is_infinite_end else f"{self.end}]"
        return f"{left}, {right}"


@dataclass
class IntegralConfig:
    """积分配置参数"""
    accuracy_level: AccuracyLevel = AccuracyLevel.MEDIUM
    split_rule: SplitRule = SplitRule.ADAPTIVE
    method: str = "simpson"
    function_type: FunctionType = FunctionType.REGULAR
    custom_tolerance: Optional[float] = None
    max_iterations: Optional[int] = None
    min_points: Optional[int] = None
    use_richardson: bool = True
    enable_progress: bool = False
    parallel_enabled: bool = False

    @property
    def tolerance(self) -> float:
        if self.custom_tolerance is not None:
            return self.custom_tolerance
        return self.accuracy_level.get_tolerance()

    @property
    def max_iter(self) -> int:
        if self.max_iterations is not None:
            return self.max_iterations
        return self.accuracy_level.get_max_iterations()

    @property
    def min_sample_points(self) -> int:
        if self.min_points is not None:
            return self.min_points
        return self.accuracy_level.get_min_points()


@dataclass
class IntegralResult:
    """积分计算结果"""
    value: float
    error_estimate: float
    intervals_used: int
    function_evaluations: int
    iterations: int
    converged: bool
    method: str
    interval: Interval
    config: IntegralConfig
    sub_results: List["IntegralResult"] = field(default_factory=list)
    metadata: Dict[str, Any] = field(default_factory=dict)

    @property
    def relative_error(self) -> float:
        if abs(self.value) > 1e-15:
            return abs(self.error_estimate / self.value)
        return float('inf')

    @property
    def accuracy_digits(self) -> float:
        if self.error_estimate > 0:
            return -math.log10(self.error_estimate)
        return float('inf')

    def __repr__(self) -> str:
        return (
            f"IntegralResult(value={self.value:.10e}, "
            f"error={self.error_estimate:.2e}, "
            f"converged={self.converged}, "
            f"method={self.method}, "
            f"evaluations={self.function_evaluations})"
        )


@dataclass
class SubIntervalResult:
    """子区间积分结果"""
    interval: Interval
    value: float
    error: float
    depth: int = 0
    needs_refinement: bool = False


@dataclass
class RichardsonResult:
    """Richardson外推结果"""
    extrapolated_value: float
    error_estimate: float
    order: int
    values: List[float] = field(default_factory=list)
    steps: List[float] = field(default_factory=list)


@dataclass
class MultiDimensionalInterval:
    """多维积分区间"""
    intervals: List[Interval]
    variable_limits: Optional[List[Callable]] = None

    @property
    def dimension(self) -> int:
        return len(self.intervals)

    def __repr__(self) -> str:
        parts = [str(iv) for iv in self.intervals]
        return " × ".join(parts)
