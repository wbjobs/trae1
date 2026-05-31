"""
数值积分自适应精度计算系统
================================

模块化的数值积分系统，支持：
- 复化梯形积分（自适应）
- 复化辛普森积分（自适应）
- 反常积分处理
- 多重积分
- 自定义积分函数、精度等级、区间拆分规则
- 误差分析与结果拟合
- 函数异常检测
- 趋势分析与性能统计
"""

from .core_types import (
    IntegralConfig,
    IntegralResult,
    Interval,
    MultiDimensionalInterval,
    SplitRule,
    AccuracyLevel,
    FunctionType,
    IntegralType,
)
from .interval import IntervalSplitter
from .adaptive import AdaptiveStepCalculator
from .accuracy import AccuracyValidator
from .fitting import ResultFitter
from .error import ErrorAnalyzer
from .trapezoidal import AdaptiveTrapezoidal
from .simpson import AdaptiveSimpson
from .improper import ImproperIntegral
from .multiple import MultipleIntegral
from .anomaly_detector import FunctionAnomalyDetector, AnomalyReport, FunctionAnalysis
from .trend_analysis import TrendFitter, PerformanceTracker, IntegrationReport, PerformanceStats, TrendAnalysis
from .main import NumericalIntegrator

__version__ = "2.0.0"
__all__ = [
    "NumericalIntegrator",
    "IntegralConfig",
    "IntegralResult",
    "Interval",
    "MultiDimensionalInterval",
    "SplitRule",
    "AccuracyLevel",
    "FunctionType",
    "IntegralType",
    "AdaptiveTrapezoidal",
    "AdaptiveSimpson",
    "ImproperIntegral",
    "MultipleIntegral",
    "FunctionAnomalyDetector",
    "AnomalyReport",
    "FunctionAnalysis",
    "TrendFitter",
    "PerformanceTracker",
    "IntegrationReport",
    "PerformanceStats",
    "TrendAnalysis",
]
