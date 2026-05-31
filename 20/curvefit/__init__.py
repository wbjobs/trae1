"""离散数据曲线拟合与插值计算系统"""

__version__ = "1.2.0"
__author__ = "CurveFitting System"

from .preprocessing import DataPreprocessor, OutlierMethod, SmoothingMethod, MissingMethod
from .lagrange import LagrangeInterpolator
from .spline import SplineInterpolator, SplineType, SplineBC, ExtrapMode
from .polynomial import PolynomialFitter
from .error_analysis import ErrorAnalyzer
from .comparison import AlgorithmComparator, ComparisonReport
from .trend import TrendPredictor, TrendModel, TrendPrediction
from .dataio import (
    BatchProcessor,
    Dataset,
    generate_adaptive_grid,
    generate_demo,
    generate_grid,
    load_csv,
    downsample,
)

__all__ = [
    "DataPreprocessor",
    "OutlierMethod",
    "SmoothingMethod",
    "MissingMethod",
    "LagrangeInterpolator",
    "SplineInterpolator",
    "SplineType",
    "SplineBC",
    "ExtrapMode",
    "PolynomialFitter",
    "ErrorAnalyzer",
    "AlgorithmComparator",
    "ComparisonReport",
    "TrendPredictor",
    "TrendModel",
    "TrendPrediction",
    "BatchProcessor",
    "Dataset",
    "generate_adaptive_grid",
    "generate_demo",
    "generate_grid",
    "load_csv",
    "downsample",
]
