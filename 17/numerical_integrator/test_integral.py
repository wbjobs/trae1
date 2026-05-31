"""
数值积分系统测试模块 v2.0
================================

测试所有积分功能模块，包括新增的异常检测、趋势分析和性能统计。
"""

import sys
import os
import math
import time

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))

from numerical_integrator import (
    NumericalIntegrator,
    IntegralConfig,
    IntegralResult,
    Interval,
    MultiDimensionalInterval,
    SplitRule,
    AccuracyLevel,
    AdaptiveTrapezoidal,
    AdaptiveSimpson,
    ImproperIntegral,
    MultipleIntegral,
    FunctionAnomalyDetector,
    AnomalyReport,
    FunctionAnalysis,
    TrendFitter,
    PerformanceTracker,
    PerformanceStats,
    TrendAnalysis,
)


class TestResults:
    """测试结果收集器"""

    def __init__(self):
        self.passed = 0
        self.failed = 0
        self.errors = []

    def add_result(self, name: str, success: bool, message: str = ""):
        if success:
            self.passed += 1
            print(f"  ✓ {name}")
        else:
            self.failed += 1
            self.errors.append((name, message))
            print(f"  ✗ {name}: {message}")

    def summary(self):
        print(f"\n{'='*60}")
        print(f"测试完成: {self.passed} 通过, {self.failed} 失败")
        if self.errors:
            print(f"\n失败的测试:")
            for name, msg in self.errors:
                print(f"  - {name}: {msg}")
        print(f"{'='*60}")
        return self.failed == 0


results = TestResults()


def test_basic_integration():
    """测试基本积分功能"""
    print("\n" + "="*60)
    print("测试基本积分功能")
    print("="*60)

    integrator = NumericalIntegrator(
        accuracy_level=AccuracyLevel.HIGH,
        method="simpson"
    )

    f1 = lambda x: x**2
    r1 = integrator.integrate(f1, 0, 1)
    expected1 = 1.0/3.0
    results.add_result(
        "∫x² dx [0,1] (辛普森)",
        abs(r1.value - expected1) < 1e-6,
        f"期望 {expected1:.10f}, 得到 {r1.value:.10f}"
    )

    f2 = lambda x: math.sin(x)
    r2 = integrator.integrate(f2, 0, math.pi)
    expected2 = 2.0
    results.add_result(
        "∫sin(x) dx [0,π] (辛普森)",
        abs(r2.value - expected2) < 1e-6,
        f"期望 {expected2:.10f}, 得到 {r2.value:.10f}"
    )

    f3 = lambda x: math.exp(-x)
    r3 = integrator.integrate(f3, 0, 1)
    expected3 = 1 - math.exp(-1)
    results.add_result(
        "∫e^(-x) dx [0,1] (辛普森)",
        abs(r3.value - expected3) < 1e-6,
        f"期望 {expected3:.10f}, 得到 {r3.value:.10f}"
    )


def test_romberg_integration():
    """测试龙贝格积分"""
    print("\n" + "="*60)
    print("测试龙贝格积分")
    print("="*60)

    integrator = NumericalIntegrator(accuracy_level=AccuracyLevel.VERY_HIGH)

    f1 = lambda x: x**3
    r1 = integrator.romberg(f1, 0, 1, n_levels=5)
    expected1 = 1.0/4.0
    results.add_result(
        "∫x³ dx [0,1] (龙贝格)",
        abs(r1.value - expected1) < 1e-8,
        f"期望 {expected1:.10f}, 得到 {r1.value:.10f}"
    )

    f2 = lambda x: math.cos(x)
    r2 = integrator.romberg(f2, 0, math.pi/2, n_levels=5)
    expected2 = 1.0
    results.add_result(
        "∫cos(x) dx [0,π/2] (龙贝格)",
        abs(r2.value - expected2) < 1e-8,
        f"期望 {expected2:.10f}, 得到 {r2.value:.10f}"
    )


def test_improper_integrals():
    """测试反常积分"""
    print("\n" + "="*60)
    print("测试反常积分")
    print("="*60)

    integrator = NumericalIntegrator(
        accuracy_level=AccuracyLevel.MEDIUM,
        method="simpson"
    )

    f1 = lambda x: 1.0 / (x**2 + 1)
    r1 = integrator.integrate_improper(f1, a=0, b=None)
    expected1 = math.pi / 2
    results.add_result(
        "∫1/(x²+1) dx [0,∞)",
        abs(r1.value - expected1) < 1e-3,
        f"期望 {expected1:.10f}, 得到 {r1.value:.10f}"
    )

    f2 = lambda x: math.exp(-x**2)
    r2 = integrator.integrate_improper(f2, a=None, b=None)
    expected2 = math.sqrt(math.pi)
    results.add_result(
        "∫e^(-x²) dx (-∞,∞)",
        abs(r2.value - expected2) < 1e-2,
        f"期望 {expected2:.10f}, 得到 {r2.value:.10f}"
    )

    f3 = lambda x: 1.0 / math.sqrt(x) if x > 0 else 0
    r3 = integrator.integrate_improper(f3, a=0, b=1, singular_points=[0])
    expected3 = 2.0
    results.add_result(
        "∫1/√x dx [0,1] (奇点)",
        abs(r3.value - expected3) < 1e-2,
        f"期望 {expected3:.10f}, 得到 {r3.value:.10f}"
    )


def test_double_integral():
    """测试二重积分"""
    print("\n" + "="*60)
    print("测试二重积分")
    print("="*60)

    integrator = NumericalIntegrator(
        accuracy_level=AccuracyLevel.MEDIUM,
        method="simpson"
    )

    f1 = lambda x, y: x * y
    r1 = integrator.integrate_double(f1, 0, 1, 0, 1)
    expected1 = 1.0/4.0
    results.add_result(
        "∫∫xy dxdy [0,1]×[0,1]",
        abs(r1.value - expected1) < 1e-1,
        f"期望 {expected1:.10f}, 得到 {r1.value:.10f}"
    )


def test_triple_integral():
    """测试三重积分"""
    print("\n" + "="*60)
    print("测试三重积分")
    print("="*60)

    integrator = NumericalIntegrator(
        accuracy_level=AccuracyLevel.LOW,
        method="simpson"
    )

    f1 = lambda x, y, z: x * y * z
    r1 = integrator.integrate_triple(f1, 0, 1, 0, 1, 0, 1)
    expected1 = 1.0/8.0
    results.add_result(
        "∫∫∫xyz dxdydz [0,1]³",
        abs(r1.value - expected1) < 1e-1,
        f"期望 {expected1:.10f}, 得到 {r1.value:.10f}"
    )


def test_anomaly_detector():
    """测试异常检测模块"""
    print("\n" + "="*60)
    print("测试异常检测模块")
    print("="*60)

    config = IntegralConfig(accuracy_level=AccuracyLevel.MEDIUM)

    f1 = lambda x: x**2
    detector1 = FunctionAnomalyDetector(f1, config)
    interval = Interval(start=0, end=1)
    report1 = detector1.detect_anomalies(interval)
    results.add_result(
        "检测平滑函数 x²",
        not report1.has_anomaly,
        f"检测到异常: {report1.has_anomaly}"
    )

    f2 = lambda x: 1.0 / x if x != 0 else float('inf')
    detector2 = FunctionAnomalyDetector(f2, config)
    interval2 = Interval(start=-1, end=1)
    report2 = detector2.detect_anomalies(interval2)
    results.add_result(
        "检测奇点函数 1/x",
        report2.has_anomaly or len(report2.singularity_positions) > 0,
        f"奇点位置: {report2.singularity_positions}"
    )

    f3 = lambda x: math.sin(x)
    detector3 = FunctionAnomalyDetector(f3, config)
    analysis3 = detector3.analyze(interval)
    results.add_result(
        "分析平滑函数 sin(x)",
        analysis3.function_type.value in ["smooth", "regular"],
        f"函数类型: {analysis3.function_type.value}"
    )

    results.add_result(
        "获取推荐方法",
        report2.recommended_method in ["trapezoidal", "simpson", "romberg"],
        f"推荐方法: {report2.recommended_method}"
    )


def test_trend_analysis():
    """测试趋势分析模块"""
    print("\n" + "="*60)
    print("测试趋势分析模块")
    print("="*60)

    fitter = TrendFitter()

    fitter.add_point(1, 0.3, 0.1, 0.001, 10)
    fitter.add_point(2, 0.33, 0.01, 0.002, 20)
    fitter.add_point(3, 0.333, 0.001, 0.003, 40)

    analysis = fitter.analyze()
    results.add_result(
        "趋势分析 - 收敛判断",
        analysis.is_converging,
        f"收敛中: {analysis.is_converging}"
    )

    results.add_result(
        "趋势分析 - 稳定性",
        analysis.stability_index > 0.5,
        f"稳定性: {analysis.stability_index:.3f}"
    )

    a, b, c = fitter.fit_error_model()
    results.add_result(
        "误差模型拟合",
        b < 0,
        f"衰减系数: {b:.4f}"
    )

    points = fitter.get_points()
    results.add_result(
        "获取收敛数据点",
        len(points) == 3,
        f"点数: {len(points)}"
    )


def test_performance_tracker():
    """测试性能统计模块"""
    print("\n" + "="*60)
    print("测试性能统计模块")
    print("="*60)

    tracker = PerformanceTracker()

    tracker.start()
    time.sleep(0.01)
    tracker.finish_setup()
    tracker.add_evaluations(100)
    time.sleep(0.01)
    tracker.stop()

    stats = tracker.get_stats()
    results.add_result(
        "性能统计 - 总时间",
        stats.total_time > 0,
        f"总时间: {stats.total_time:.4f}s"
    )

    results.add_result(
        "性能统计 - 评估次数",
        stats.total_evaluations == 100,
        f"评估次数: {stats.total_evaluations}"
    )

    results.add_result(
        "性能统计 - 每秒评估",
        stats.evaluations_per_second > 0,
        f"每秒评估: {stats.evaluations_per_second:.1f}"
    )

    elapsed = tracker.get_elapsed_time()
    results.add_result(
        "获取已用时间",
        elapsed > 0,
        f"已用时间: {elapsed:.4f}s"
    )


def test_integration_with_analysis():
    """测试带分析的积分"""
    print("\n" + "="*60)
    print("测试带分析的积分")
    print("="*60)

    integrator = NumericalIntegrator(
        accuracy_level=AccuracyLevel.HIGH,
        method="simpson"
    )

    f = lambda x: x**2
    result = integrator.integrate(f, 0, 1, enable_analysis=True)
    expected = 1.0/3.0

    results.add_result(
        "带分析的积分 - 结果正确",
        abs(result.value - expected) < 1e-6,
        f"期望 {expected:.10f}, 得到 {result.value:.10f}"
    )

    report = integrator.get_performance_report()
    results.add_result(
        "获取性能报告",
        report is not None,
        f"报告: {'存在' if report else '不存在'}"
    )

    if report is not None:
        results.add_result(
            "报告包含性能统计",
            report.performance.total_time > 0,
            f"计算时间: {report.performance.total_time:.4f}s"
        )

    trend = integrator.get_trend_analysis()
    results.add_result(
        "获取趋势分析",
        trend is not None,
        f"趋势分析: {'存在' if trend else '不存在'}"
    )


def test_analyze_function():
    """测试函数分析功能"""
    print("\n" + "="*60)
    print("测试函数分析功能")
    print("="*60)

    integrator = NumericalIntegrator(
        accuracy_level=AccuracyLevel.MEDIUM
    )

    f = lambda x: x**2
    report = integrator.analyze_function(f, 0, 1)

    results.add_result(
        "函数分析 - 报告生成",
        report is not None,
        f"报告: {'存在' if report else '不存在'}"
    )

    results.add_result(
        "函数分析 - 推荐方法",
        report.recommended_method in ["trapezoidal", "simpson", "romberg"],
        f"推荐方法: {report.recommended_method}"
    )

    results.add_result(
        "函数分析 - 难度评估",
        0 <= report.estimated_difficulty <= 1,
        f"难度: {report.estimated_difficulty:.2f}"
    )


def test_complex_functions():
    """测试复杂函数积分"""
    print("\n" + "="*60)
    print("测试复杂函数积分")
    print("="*60)

    integrator = NumericalIntegrator(
        accuracy_level=AccuracyLevel.HIGH,
        method="simpson"
    )

    f1 = lambda x: math.sin(x) * math.cos(x)
    r1 = integrator.integrate(f1, 0, math.pi/2)
    expected1 = 0.5
    results.add_result(
        "∫sin(x)cos(x) dx",
        abs(r1.value - expected1) < 1e-4,
        f"期望 {expected1:.10f}, 得到 {r1.value:.10f}"
    )

    f2 = lambda x: x * math.exp(-x**2)
    r2 = integrator.integrate(f2, 0, 1)
    expected2 = (1 - math.exp(-1)) / 2
    results.add_result(
        "∫x·e^(-x²) dx",
        abs(r2.value - expected2) < 1e-4,
        f"期望 {expected2:.10f}, 得到 {r2.value:.10f}"
    )


def test_convergence():
    """测试收敛性"""
    print("\n" + "="*60)
    print("测试收敛性")
    print("="*60)

    integrator = NumericalIntegrator(
        accuracy_level=AccuracyLevel.VERY_HIGH,
        method="simpson"
    )

    f = lambda x: math.sin(x)
    r = integrator.integrate(f, 0, math.pi)

    results.add_result(
        "收敛检查",
        r.converged,
        f"收敛: {r.converged}, 迭代: {r.iterations}"
    )

    results.add_result(
        "精度位数",
        r.accuracy_digits > 4,
        f"精度位数: {r.accuracy_digits:.1f}"
    )


def test_performance():
    """测试性能和超时保护"""
    print("\n" + "="*60)
    print("测试性能和超时保护")
    print("="*60)

    integrator = NumericalIntegrator(
        accuracy_level=AccuracyLevel.HIGH,
        method="simpson"
    )

    f = lambda x: math.sin(x)

    start = time.time()
    r = integrator.integrate(f, 0, 10 * math.pi)
    elapsed = time.time() - start

    results.add_result(
        "性能测试 (10周期)",
        r.converged and elapsed < 15.0,
        f"耗时: {elapsed:.3f}s, 评估次数: {r.function_evaluations}"
    )


def test_configuration():
    """测试配置功能"""
    print("\n" + "="*60)
    print("测试配置功能")
    print("="*60)

    config = IntegralConfig(
        accuracy_level=AccuracyLevel.HIGH,
        split_rule=SplitRule.ADAPTIVE,
        method="simpson",
        custom_tolerance=1e-10,
        max_iterations=50
    )

    results.add_result(
        "自定义容差",
        config.tolerance == 1e-10,
        f"容差: {config.tolerance}"
    )

    results.add_result(
        "自定义迭代次数",
        config.max_iter == 50,
        f"最大迭代: {config.max_iter}"
    )


def main():
    """运行所有测试"""
    print("\n" + "="*60)
    print("数值积分自适应精度计算系统 v2.0 - 测试套件")
    print("="*60)

    test_basic_integration()
    test_romberg_integration()
    test_improper_integrals()
    test_double_integral()
    test_triple_integral()
    test_anomaly_detector()
    test_trend_analysis()
    test_performance_tracker()
    test_integration_with_analysis()
    test_analyze_function()
    test_complex_functions()
    test_convergence()
    test_performance()
    test_configuration()

    return results.summary()


if __name__ == "__main__":
    success = main()
    sys.exit(0 if success else 1)
