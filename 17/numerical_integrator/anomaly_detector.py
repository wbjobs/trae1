"""
积分函数异常检测模块
================================

提供积分函数的异常检测和诊断功能：
- 函数值异常检测（NaN、Inf、溢出）
- 奇点自动检测
- 函数特性分析（平滑、振荡、奇异）
- 收敛性预测
- 数值稳定性评估
"""

from typing import Callable, List, Tuple, Optional, Dict
from dataclasses import dataclass, field
from enum import Enum
import math

from .core_types import Interval, FunctionType, IntegralConfig


class AnomalyType(Enum):
    """异常类型"""
    NONE = "none"
    NAN = "nan"
    INF = "inf"
    OVERFLOW = "overflow"
    SINGULARITY = "singularity"
    OSCILLATION = "oscillation"
    RAPID_CHANGE = "rapid_change"


@dataclass
class FunctionSample:
    """函数采样点"""
    x: float
    y: float
    valid: bool
    anomaly: AnomalyType
    derivative: float = 0.0
    second_derivative: float = 0.0


@dataclass
class FunctionAnalysis:
    """函数分析结果"""
    function_type: FunctionType
    anomaly_points: List[float]
    singularity_points: List[float]
    oscillation_regions: List[Tuple[float, float]]
    smoothness_score: float
    stability_score: float
    sample_values: List[FunctionSample]
    min_value: float
    max_value: float
    mean_value: float
    std_deviation: float


@dataclass
class AnomalyReport:
    """异常检测报告"""
    has_anomaly: bool
    anomaly_types: List[AnomalyType]
    singularity_positions: List[float]
    warning_messages: List[str]
    recommendations: List[str]
    estimated_difficulty: float
    recommended_method: str
    recommended_accuracy: str


class FunctionAnomalyDetector:
    """
    积分函数异常检测器

    提供函数特性分析和异常检测功能，用于：
    - 自动检测函数奇点
    - 分析函数特性（平滑、振荡等）
    - 评估数值稳定性
    - 推荐合适的积分方法
    """

    def __init__(
        self,
        function: Callable,
        config: Optional[IntegralConfig] = None
    ):
        self.function = function
        self.config = config or IntegralConfig()
        self._sample_cache: Dict[float, FunctionSample] = {}

    def analyze(
        self,
        interval: Interval,
        n_samples: int = 100
    ) -> FunctionAnalysis:
        """
        分析函数在区间上的特性

        Args:
            interval: 积分区间
            n_samples: 采样点数量

        Returns:
            函数分析结果
        """
        samples = self._sample_function(interval, n_samples)
        anomaly_points = [s.x for s in samples if s.anomaly != AnomalyType.NONE]
        singularity_points = [s.x for s in samples if s.anomaly == AnomalyType.SINGULARITY]
        oscillation_regions = self._detect_oscillations(samples)
        smoothness = self._calculate_smoothness(samples)
        stability = self._calculate_stability(samples)

        values = [s.y for s in samples if s.valid]
        if values:
            min_val = min(values)
            max_val = max(values)
            mean_val = sum(values) / len(values)
            std_val = math.sqrt(sum((v - mean_val) ** 2 for v in values) / len(values))
        else:
            min_val = max_val = mean_val = std_val = 0.0

        func_type = self._classify_function(
            samples, smoothness, stability, singularity_points
        )

        return FunctionAnalysis(
            function_type=func_type,
            anomaly_points=anomaly_points,
            singularity_points=singularity_points,
            oscillation_regions=oscillation_regions,
            smoothness_score=smoothness,
            stability_score=stability,
            sample_values=samples,
            min_value=min_val,
            max_value=max_val,
            mean_value=mean_val,
            std_deviation=std_val
        )

    def detect_anomalies(
        self,
        interval: Interval,
        n_samples: int = 200
    ) -> AnomalyReport:
        """
        检测函数异常并生成报告

        Args:
            interval: 积分区间
            n_samples: 采样点数量

        Returns:
            异常检测报告
        """
        analysis = self.analyze(interval, n_samples)

        warnings = []
        recommendations = []
        anomaly_types = set()

        for sample in analysis.sample_values:
            if sample.anomaly != AnomalyType.NONE:
                anomaly_types.add(sample.anomaly)

        if analysis.singularity_points:
            warnings.append(
                f"检测到 {len(analysis.singularity_points)} 个奇点: "
                f"{[f'{x:.6f}' for x in analysis.singularity_points[:5]]}"
            )
            recommendations.append("建议使用奇点处理模式")

        if analysis.smoothness_score < 0.3:
            warnings.append("函数在区间上不够平滑")
            recommendations.append("建议增加采样点密度")

        if analysis.stability_score < 0.5:
            warnings.append("函数数值稳定性较差")
            recommendations.append("建议使用更高精度或自适应积分")

        if analysis.function_type == FunctionType.OSCILLATORY:
            warnings.append("检测到函数振荡特性")
            recommendations.append("建议使用振荡积分方法或增加采样频率")

        if analysis.function_type == FunctionType.SINGULAR:
            warnings.append("函数具有奇异性")
            recommendations.append("建议使用反常积分模式")

        difficulty = self._calculate_difficulty(analysis)

        if difficulty < 0.3:
            recommended_method = "trapezoidal"
            recommended_accuracy = "low"
        elif difficulty < 0.6:
            recommended_method = "simpson"
            recommended_accuracy = "medium"
        elif difficulty < 0.8:
            recommended_method = "simpson"
            recommended_accuracy = "high"
        else:
            recommended_method = "romberg"
            recommended_accuracy = "very_high"

        return AnomalyReport(
            has_anomaly=len(anomaly_types) > 0 or len(warnings) > 0,
            anomaly_types=list(anomaly_types),
            singularity_positions=analysis.singularity_points,
            warning_messages=warnings,
            recommendations=recommendations,
            estimated_difficulty=difficulty,
            recommended_method=recommended_method,
            recommended_accuracy=recommended_accuracy
        )

    def _sample_function(
        self,
        interval: Interval,
        n_samples: int
    ) -> List[FunctionSample]:
        """
        对函数进行采样

        Args:
            interval: 积分区间
            n_samples: 采样点数量

        Returns:
            采样点列表
        """
        samples = []
        a, b = interval.start, interval.end

        if interval.is_infinite_start or interval.is_infinite_end:
            a = interval.start if not interval.is_infinite_start else -100.0
            b = interval.end if not interval.is_infinite_end else 100.0

        dx = (b - a) / (n_samples - 1) if n_samples > 1 else 1.0

        for i in range(n_samples):
            x = a + i * dx
            sample = self._evaluate_point(x, dx)
            samples.append(sample)

        return samples

    def _evaluate_point(self, x: float, dx: float) -> FunctionSample:
        """
        评估单个点的函数值和导数

        Args:
            x: 采样点位置
            dx: 步长（用于导数计算）

        Returns:
            函数采样点
        """
        if x in self._sample_cache:
            return self._sample_cache[x]

        valid = True
        anomaly = AnomalyType.NONE
        y = 0.0
        derivative = 0.0
        second_derivative = 0.0

        try:
            y = self.function(x)

            if math.isnan(y):
                valid = False
                anomaly = AnomalyType.NAN
            elif math.isinf(y):
                valid = False
                anomaly = AnomalyType.INF
            elif abs(y) > 1e100:
                anomaly = AnomalyType.OVERFLOW

            if valid and abs(y) < 1e100:
                try:
                    y_plus = self.function(x + dx)
                    y_minus = self.function(x - dx)
                    derivative = (y_plus - y_minus) / (2 * dx) if abs(dx) > 1e-15 else 0.0
                    second_derivative = (y_plus - 2 * y + y_minus) / (dx * dx) if abs(dx) > 1e-15 else 0.0

                    if abs(derivative) > 1e50 or abs(second_derivative) > 1e50:
                        anomaly = AnomalyType.RAPID_CHANGE
                except (ValueError, ZeroDivisionError, OverflowError):
                    pass

        except ValueError:
            valid = False
            anomaly = AnomalyType.NAN
        except ZeroDivisionError:
            valid = False
            anomaly = AnomalyType.SINGULARITY
        except OverflowError:
            valid = False
            anomaly = AnomalyType.OVERFLOW
        except Exception:
            valid = False
            anomaly = AnomalyType.SINGULARITY

        sample = FunctionSample(
            x=x,
            y=y if valid else 0.0,
            valid=valid,
            anomaly=anomaly,
            derivative=derivative,
            second_derivative=second_derivative
        )

        self._sample_cache[x] = sample
        return sample

    def _detect_oscillations(
        self,
        samples: List[FunctionSample]
    ) -> List[Tuple[float, float]]:
        """
        检测振荡区域

        Args:
            samples: 采样点列表

        Returns:
            振荡区域列表
        """
        regions = []
        if len(samples) < 3:
            return regions

        sign_changes = 0
        in_oscillation = False
        start_x = samples[0].x

        for i in range(1, len(samples) - 1):
            if samples[i].derivative * samples[i-1].derivative < 0:
                sign_changes += 1
                if sign_changes >= 3 and not in_oscillation:
                    start_x = samples[i-2].x
                    in_oscillation = True
            else:
                if sign_changes < 3 and in_oscillation:
                    in_oscillation = False
                sign_changes = max(0, sign_changes - 1)

            if in_oscillation and sign_changes >= 3:
                pass

            if not in_oscillation and sign_changes >= 3:
                start_x = samples[i-2].x
                in_oscillation = True
            elif in_oscillation and sign_changes < 2:
                regions.append((start_x, samples[i].x))
                in_oscillation = False

        if in_oscillation:
            regions.append((start_x, samples[-1].x))

        return regions

    def _calculate_smoothness(
        self,
        samples: List[FunctionSample]
    ) -> float:
        """
        计算函数平滑度

        Args:
            samples: 采样点列表

        Returns:
            平滑度评分 (0-1)
        """
        valid_samples = [s for s in samples if s.valid]
        if len(valid_samples) < 3:
            return 0.0

        second_derivatives = [abs(s.second_derivative) for s in valid_samples]
        if not second_derivatives:
            return 1.0

        max_second_derivative = max(second_derivatives)
        if max_second_derivative < 1e-10:
            return 1.0

        smoothness = 1.0 / (1.0 + max_second_derivative / 1000.0)
        return max(0.0, min(1.0, smoothness))

    def _calculate_stability(
        self,
        samples: List[FunctionSample]
    ) -> float:
        """
        计算数值稳定性

        Args:
            samples: 采样点列表

        Returns:
            稳定性评分 (0-1)
        """
        valid_count = sum(1 for s in samples if s.valid)
        if len(samples) == 0:
            return 0.0

        validity_ratio = valid_count / len(samples)

        valid_samples = [s for s in samples if s.valid]
        if len(valid_samples) < 2:
            return validity_ratio * 0.5

        values = [s.y for s in valid_samples]
        mean_val = sum(values) / len(values)

        if abs(mean_val) < 1e-15:
            value_stability = 1.0
        else:
            std_val = math.sqrt(sum((v - mean_val) ** 2 for v in values) / len(values))
            cv = std_val / abs(mean_val)
            value_stability = 1.0 / (1.0 + cv)

        return max(0.0, min(1.0, 0.5 * validity_ratio + 0.5 * value_stability))

    def _classify_function(
        self,
        samples: List[FunctionSample],
        smoothness: float,
        stability: float,
        singularity_points: List[float]
    ) -> FunctionType:
        """
        分类函数类型

        Args:
            samples: 采样点列表
            smoothness: 平滑度
            stability: 稳定性
            singularity_points: 奇点列表

        Returns:
            函数类型
        """
        if singularity_points:
            return FunctionType.SINGULAR

        oscillation_regions = self._detect_oscillations(samples)
        if len(oscillation_regions) > 0:
            return FunctionType.OSCILLATORY

        if smoothness > 0.7 and stability > 0.7:
            return FunctionType.SMOOTH

        return FunctionType.REGULAR

    def _calculate_difficulty(self, analysis: FunctionAnalysis) -> float:
        """
        计算积分难度

        Args:
            analysis: 函数分析结果

        Returns:
            难度值 (0-1)
        """
        difficulty = 0.0

        if analysis.singularity_points:
            difficulty += 0.4

        if analysis.function_type == FunctionType.OSCILLATORY:
            difficulty += 0.3

        if analysis.function_type == FunctionType.SINGULAR:
            difficulty += 0.3

        difficulty += (1.0 - analysis.smoothness_score) * 0.2
        difficulty += (1.0 - analysis.stability_score) * 0.2

        if analysis.std_deviation > 0:
            cv = analysis.std_deviation / (abs(analysis.mean_value) + 1e-15)
            difficulty += min(0.2, cv / 10.0)

        return min(1.0, max(0.0, difficulty))

    def clear_cache(self):
        """清除采样缓存"""
        self._sample_cache.clear()
