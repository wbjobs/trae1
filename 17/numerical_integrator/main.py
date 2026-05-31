"""
主程序入口和API整合模块
================================

提供统一的API接口，整合所有积分功能模块。
支持异常检测、趋势分析、性能统计等高级功能。
"""

from typing import Callable, List, Tuple, Optional, Union, Dict
from dataclasses import dataclass, field
import math
import time

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
from .trapezoidal import AdaptiveTrapezoidal
from .simpson import AdaptiveSimpson
from .improper import ImproperIntegral
from .multiple import MultipleIntegral
from .interval import IntervalSplitter
from .adaptive import AdaptiveStepCalculator
from .accuracy import AccuracyValidator
from .fitting import ResultFitter
from .error import ErrorAnalyzer
from .anomaly_detector import FunctionAnomalyDetector, AnomalyReport, AnomalyType
from .trend_analysis import TrendFitter, PerformanceTracker, IntegrationReport


class NumericalIntegrator:
    """
    数值积分器 - 主API类

    整合所有积分功能，提供统一的调用接口。

    支持的功能：
    - 复化梯形积分（自适应）
    - 复化辛普森积分（自适应）
    - 龙贝格积分
    - 反常积分（无穷区间、奇点）
    - 多重积分（二重、三重、N维）
    - 自定义积分函数
    - 自定义精度等级
    - 自定义区间拆分规则
    - 函数异常检测
    - 收敛趋势分析
    - 计算性能统计
    """

    def __init__(
        self,
        config: Optional[IntegralConfig] = None,
        **kwargs
    ):
        self.config = config or IntegralConfig(**kwargs)
        self.splitter = IntervalSplitter(config=self.config)
        self.step_calculator = AdaptiveStepCalculator(config=self.config)
        self.validator = AccuracyValidator(self.config)
        self.fitter = ResultFitter()
        self.error_analyzer = ErrorAnalyzer(self.config)
        self.anomaly_detector: Optional[FunctionAnomalyDetector] = None
        self.trend_fitter = TrendFitter()
        self.performance_tracker = PerformanceTracker()

        self._last_result: Optional[IntegralResult] = None
        self._last_report: Optional[IntegrationReport] = None
        self._convergence_history: List[float] = []

    def integrate(
        self,
        function: Callable,
        a: float,
        b: float,
        method: Optional[str] = None,
        enable_analysis: bool = False,
        **kwargs
    ) -> IntegralResult:
        """
        计算定积分

        ∫(a to b) f(x) dx

        Args:
            function: 被积函数 f(x)
        a: 积分下限
        b: 积分上限
        method: 积分方法 ('trapezoidal', 'simpson', 'romberg')
        enable_analysis: 是否启用异常检测和趋势分析
        **kwargs: 额外配置参数

        Returns:
            积分结果
        """
        config = self._update_config(method=method, **kwargs)

        if enable_analysis:
            self.performance_tracker.start()
            self.anomaly_detector = FunctionAnomalyDetector(function, config)
            self.trend_fitter.clear()

        interval = Interval(start=a, end=b)

        if enable_analysis:
            analysis = self.anomaly_detector.analyze(interval)
            if analysis.function_type == FunctionType.SINGULAR or analysis.singularity_points:
                self.performance_tracker.finish_setup()
                return self._integrate_improper(function, interval, config, enable_analysis)
            self.performance_tracker.finish_setup()

        if not interval.is_finite:
            return self._integrate_improper(function, interval, config, enable_analysis)

        result = self._integrate_definite(function, interval, config)

        if enable_analysis:
            self.performance_tracker.stop()
            self._generate_report(result)

        return result

    def integrate_improper(
        self,
        function: Callable,
        a: Optional[float] = None,
        b: Optional[float] = None,
        singular_points: Optional[List[float]] = None,
        enable_analysis: bool = False,
        **kwargs
    ) -> IntegralResult:
        """
        计算反常积分

        Args:
            function: 被积函数
            a: 积分下限（None表示-∞）
            b: 积分上限（None表示+∞）
            singular_points: 奇点位置列表
            enable_analysis: 是否启用异常检测
            **kwargs: 额外配置参数

        Returns:
            积分结果
        """
        config = self._update_config(**kwargs)

        if enable_analysis:
            self.performance_tracker.start()
            self.anomaly_detector = FunctionAnomalyDetector(function, config)
            self.trend_fitter.clear()

        interval = Interval(
            start=a if a is not None else 0.0,
            end=b if b is not None else 0.0,
            is_infinite_start=a is None,
            is_infinite_end=b is None,
            singular_points=singular_points or []
        )

        if enable_analysis:
            self.performance_tracker.finish_setup()

        result = self._integrate_improper(function, interval, config, enable_analysis)

        if enable_analysis:
            self.performance_tracker.stop()
            self._generate_report(result)

        return result

    def integrate_multiple(
        self,
        function: Callable,
        limits: List[Tuple[float, float]],
        **kwargs
    ) -> IntegralResult:
        """
        计算多重积分（矩形区域）

        Args:
            function: 被积函数 f(x1, x2, ..., xn)
        limits: 各变量的积分限 [(a1, b1), (a2, b2), ...]
        **kwargs: 额外配置参数

        Returns:
            积分结果
        """
        config = self._update_config(**kwargs)

        intervals = [
            Interval(start=lim[0], end=lim[1])
            for lim in limits
        ]

        multi_interval = MultiDimensionalInterval(intervals=intervals)

        integrator = MultipleIntegral(function, config)
        result = integrator.integrate(multi_interval)

        self._last_result = result
        return result

    def integrate_double(
        self,
        function: Callable,
        a: float,
        b: float,
        c: float,
        d: float,
        **kwargs
    ) -> IntegralResult:
        """
        计算二重积分（矩形区域）

        ∫(a to b) ∫(c to d) f(x,y) dy dx

        Args:
            function: 被积函数 f(x, y)
            a: x下限
            b: x上限
            c: y下限
            d: y上限
            **kwargs: 额外配置参数

        Returns:
            积分结果
        """
        config = self._update_config(**kwargs)

        integrator = MultipleIntegral(function, config)
        result = integrator.integrate_rectangular(a, b, c, d)

        self._last_result = result
        return result

    def integrate_triple(
        self,
        function: Callable,
        a: float,
        b: float,
        c: float,
        d: float,
        e: float,
        f: float,
        **kwargs
    ) -> IntegralResult:
        """
        计算三重积分（长方体区域）

        ∫(a to b) ∫(c to d) ∫(e to f) f(x,y,z) dz dy dx

        Args:
            function: 被积函数 f(x, y, z)
            a: x下限
            b: x上限
            c: y下限
            d: y上限
            e: z下限
            f: z上限
            **kwargs: 额外配置参数

        Returns:
            积分结果
        """
        config = self._update_config(**kwargs)

        integrator = MultipleIntegral(function, config)
        result = integrator.integrate_box((a, b), (c, d), (e, f))

        self._last_result = result
        return result

    def integrate_variable(
        self,
        function: Callable,
        a: float,
        b: float,
        y_lower: Callable,
        y_upper: Callable,
        **kwargs
    ) -> IntegralResult:
        """
        计算二重积分（一般区域）

        ∫(a to b) ∫(y_lower(x) to y_upper(x)) f(x,y) dy dx

        Args:
            function: 被积函数 f(x, y)
            a: x下限
            b: x上限
            y_lower: y下限函数 y_lower(x)
            y_upper: y上限函数 y_upper(x)
            **kwargs: 额外配置参数

        Returns:
            积分结果
        """
        config = self._update_config(**kwargs)

        integrator = MultipleIntegral(function, config)
        result = integrator.integrate_variable_limits(a, b, y_lower, y_upper)

        self._last_result = result
        return result

    def romberg(
        self,
        function: Callable,
        a: float,
        b: float,
        n_levels: int = 6,
        **kwargs
    ) -> IntegralResult:
        """
        龙贝格积分

        使用Romberg外推法提高积分精度。

        Args:
            function: 被积函数
            a: 积分下限
            b: 积分上限
            n_levels: 外推层数
            **kwargs: 额外配置参数

        Returns:
            积分结果
        """
        config = self._update_config(**kwargs)

        interval = Interval(start=a, end=b)
        integrator = AdaptiveSimpson(function, config)
        result = integrator.romberg_integrate(interval, n_levels)

        self._last_result = result
        self._convergence_history = integrator.get_convergence_history()

        return result

    def analyze_function(
        self,
        function: Callable,
        a: float,
        b: float,
        n_samples: int = 100
    ) -> AnomalyReport:
        """
        分析被积函数特性

        Args:
            function: 被积函数
            a: 积分下限
            b: 积分上限
            n_samples: 采样点数量

        Returns:
            异常检测报告
        """
        detector = FunctionAnomalyDetector(function, self.config)
        interval = Interval(start=a, end=b)
        return detector.detect_anomalies(interval, n_samples)

    def get_performance_report(self) -> Optional[IntegrationReport]:
        """
        获取上次计算的性能报告

        Returns:
            积分报告（包含性能统计和趋势分析）
        """
        return self._last_report

    def get_trend_analysis(self):
        """
        获取收敛趋势分析

        Returns:
            趋势分析结果
        """
        return self.trend_fitter.analyze()

    def get_performance_stats(self):
        """
        获取性能统计

        Returns:
            性能统计数据
        """
        return self.performance_tracker.get_stats()

    def adaptive_integrate(
        self,
        function: Callable,
        a: float,
        b: float,
        method: str = "simpson",
        target_accuracy: Optional[float] = None,
        **kwargs
    ) -> IntegralResult:
        """
        自适应积分

        根据函数特性自动选择最佳积分策略。

        Args:
            function: 被积函数
            a: 积分下限
            b: 积分上限
            method: 基础积分方法
            target_accuracy: 目标精度
            **kwargs: 额外配置参数

        Returns:
            积分结果
        """
        config = self._update_config(
            method=method,
            custom_tolerance=target_accuracy,
            **kwargs
        )

        self.splitter.function = function
        self.step_calculator.function = function

        interval = Interval(start=a, end=b)

        if not interval.is_finite:
            return self._integrate_improper(function, interval, config)

        detector = FunctionAnomalyDetector(function, config)
        analysis = detector.analyze(interval)

        if analysis.function_type == FunctionType.SINGULAR:
            singular_regions = self.splitter.find_singular_regions(interval)
            if singular_regions:
                all_points = []
                for region in singular_regions:
                    all_points.extend([region.start, region.end])
                return self.integrate_improper(
                    function, a, b, singular_points=all_points
                )

        if analysis.function_type == FunctionType.OSCILLATORY:
            config = self._update_config(
                custom_tolerance=max(config.tolerance * 0.1, 1e-15),
                **kwargs
            )

        return self._integrate_definite(function, interval, config)

    def compare_methods(
        self,
        function: Callable,
        a: float,
        b: float,
        reference_value: Optional[float] = None
    ) -> Dict[str, IntegralResult]:
        """
        比较不同积分方法的结果

        Args:
            function: 被积函数
            a: 积分下限
            b: 积分上限
            reference_value: 参考值（可选）

        Returns:
            各方法的结果字典
        """
        results = {}

        for method in ["trapezoidal", "simpson", "romberg"]:
            try:
                if method == "romberg":
                    result = self.romberg(function, a, b)
                else:
                    result = self.integrate(function, a, b, method=method)
                results[method] = result
            except Exception as e:
                results[method] = IntegralResult(
                    value=float('nan'),
                    error_estimate=float('inf'),
                    intervals_used=0,
                    function_evaluations=0,
                    iterations=0,
                    converged=False,
                    method=method,
                    interval=Interval(start=a, end=b),
                    config=self.config
                )

        return results

    def analyze_result(
        self,
        result: Optional[IntegralResult] = None
    ) -> Dict:
        """
        分析积分结果

        Args:
            result: 积分结果（默认使用上次计算结果）

        Returns:
            分析结果字典
        """
        if result is None:
            result = self._last_result

        if result is None:
            return {}

        error_summary = self.error_analyzer.get_error_summary(result)
        error_breakdown = self.error_analyzer.analyze_error(result)

        return {
            "error_summary": error_summary,
            "error_breakdown": error_breakdown,
            "confidence_interval": self.error_analyzer.calculate_error_bounds(result),
            "result_details": {
                "value": result.value,
                "method": result.method,
                "converged": result.converged,
                "iterations": result.iterations,
                "function_evaluations": result.function_evaluations,
            }
        }

    def fit_results(
        self,
        x_values: List[float],
        y_values: List[float],
        method: str = "polynomial",
        degree: int = 3
    ):
        """
        拟合积分结果数据

        Args:
            x_values: x坐标列表
            y_values: y坐标列表
            method: 拟合方法 ('polynomial', 'spline', 'exponential', 'logarithmic')
            degree: 多项式阶数（多项式拟合时使用）

        Returns:
            拟合结果
        """
        if method == "polynomial":
            return self.fitter.fit_polynomial(x_values, y_values, degree)
        elif method == "spline":
            return self.fitter.fit_cubic_spline(x_values, y_values)
        elif method == "exponential":
            return self.fitter.fit_exponential(x_values, y_values)
        elif method == "logarithmic":
            return self.fitter.fit_logarithmic(x_values, y_values)
        else:
            raise ValueError(f"未知的拟合方法: {method}")

    def get_convergence_history(self) -> List[float]:
        """
        获取收敛历史

        Returns:
            收敛历史值列表
        """
        return self._convergence_history.copy()

    def get_last_result(self) -> Optional[IntegralResult]:
        """
        获取上次积分结果

        Returns:
            上次的积分结果
        """
        return self._last_result

    def _update_config(
        self,
        method: Optional[str] = None,
        accuracy_level: Optional[AccuracyLevel] = None,
        split_rule: Optional[SplitRule] = None,
        custom_tolerance: Optional[float] = None,
        max_iterations: Optional[int] = None,
        use_richardson: Optional[bool] = None,
        **kwargs
    ) -> IntegralConfig:
        """
        更新配置

        Args:
            method: 积分方法
            accuracy_level: 精度等级
            split_rule: 拆分规则
            custom_tolerance: 自定义容差
            max_iterations: 最大迭代次数
            use_richardson: 是否使用Richardson外推
            **kwargs: 额外参数

        Returns:
            更新后的配置
        """
        config = IntegralConfig(
            accuracy_level=accuracy_level or self.config.accuracy_level,
            split_rule=split_rule or self.config.split_rule,
            method=method or self.config.method,
            custom_tolerance=custom_tolerance or self.config.custom_tolerance,
            max_iterations=max_iterations or self.config.max_iterations,
            use_richardson=use_richardson if use_richardson is not None else self.config.use_richardson,
        )

        self.config = config
        return config

    def _integrate_definite(
        self,
        function: Callable,
        interval: Interval,
        config: IntegralConfig,
    ) -> IntegralResult:
        """
        计算定积分

        Args:
            function: 被积函数
            interval: 积分区间
            config: 积分配置

        Returns:
            积分结果
        """
        method = config.method.lower()

        if method == "trapezoidal":
            integrator = AdaptiveTrapezoidal(function, config)
        else:
            integrator = AdaptiveSimpson(function, config)

        result = integrator.integrate(interval)
        self._last_result = result
        self._convergence_history = integrator.get_convergence_history()

        return result

    def _integrate_improper(
        self,
        function: Callable,
        interval: Interval,
        config: IntegralConfig,
        enable_analysis: bool = False,
    ) -> IntegralResult:
        """
        计算反常积分

        Args:
            function: 被积函数
            interval: 积分区间
            config: 积分配置
            enable_analysis: 是否启用分析

        Returns:
            积分结果
        """
        handler = ImproperIntegral(function, config)
        result = handler.integrate(interval)

        self._last_result = result
        return result

    def _generate_report(self, result: IntegralResult):
        """生成积分报告"""
        performance = self.performance_tracker.get_stats()
        trend = self.trend_fitter.analyze()

        recommendations = []
        warnings = []

        if not result.converged:
            recommendations.append("建议增加迭代次数或降低精度要求")
            warnings.append("积分未完全收敛")

        if result.error_estimate > self.config.tolerance:
            recommendations.append(f"当前误差超过目标精度，建议使用更高精度等级")
            warnings.append("积分精度可能不足")

        self._last_report = IntegrationReport(
            result=result,
            performance=performance,
            trend_analysis=trend,
            recommendations=recommendations,
            warnings=warnings,
        )

    def __repr__(self) -> str:
        return (
            f"NumericalIntegrator("
            f"method={self.config.method}, "
            f"accuracy={self.config.accuracy_level.value})"
        )
