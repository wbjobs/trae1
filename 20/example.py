"""示例脚本：演示离散数据曲线拟合与插值计算系统的完整工作流

包含：
- 智能异常点剔除（SMART / RESIDUAL 方法）
- 平滑样条 / 单调样条 / 精确样条对比
- 多算法自动对比与最优推荐
- 趋势预测（含 Bootstrap 置信区间）
- 边界外推保护
- 批量数据处理

运行方式：python example.py
"""

from __future__ import annotations

import os

import matplotlib
matplotlib.use("Agg")

from matplotlib import font_manager

_CJK_CANDIDATES = [
    "Microsoft YaHei", "SimHei", "SimSun", "Noto Sans CJK SC",
    "Noto Sans CJK", "Arial Unicode MS", "PingFang SC",
    "Source Han Sans SC", "WenQuanYi Zen Hei",
]
_available = {f.name for f in font_manager.fontManager.ttflist}
_pick = next((n for n in _CJK_CANDIDATES if n in _available), "DejaVu Sans")
matplotlib.rcParams["font.sans-serif"] = [_pick, "DejaVu Sans"]
matplotlib.rcParams["axes.unicode_minus"] = False

import matplotlib.pyplot as plt
import numpy as np

from curvefit import (
    AlgorithmComparator,
    DataPreprocessor,
    ErrorAnalyzer,
    ExtrapMode,
    LagrangeInterpolator,
    OutlierMethod,
    PolynomialFitter,
    SmoothingMethod,
    SplineBC,
    SplineInterpolator,
    SplineType,
    TrendModel,
    TrendPredictor,
    downsample,
    generate_adaptive_grid,
    generate_demo,
    generate_grid,
)
from curvefit.dataio import BatchProcessor, BatchResult, Dataset
from curvefit.lagrange import LagrangeResult
from curvefit.polynomial import PolynomialResult
from curvefit.preprocessing import MissingMethod
from curvefit.spline import SplineResult
from curvefit.trend import TrendPrediction


OUTPUT_DIR = "output_example"
os.makedirs(OUTPUT_DIR, exist_ok=True)


def main() -> None:
    print("=" * 70)
    print("离散数据曲线拟合与插值计算系统 - 综合示例（v1.2.0）")
    print("=" * 70)

    # ===== 1. 生成带异常点的高密度数据 =====
    print("\n" + "=" * 70)
    print("[1] 生成演示数据（50样本 + 噪声 + 3个故意异常点 + 缺失值）")
    rng = np.random.default_rng(2026)
    x = np.linspace(-5.0, 5.0, 50)
    y_true = np.sin(x) + 0.2 * x**2 - 0.01 * x**3
    y = y_true + rng.normal(0.0, 0.3, size=50)
    # 人为注入 3 个异常点
    y[12] = y[12] + 5.0
    y[28] = y[28] - 4.5
    y[40] = y[40] + 4.0
    # 缺失值
    y[5] = np.nan
    y[22] = np.nan
    y[38] = np.nan
    raw = Dataset(x=x, y=y, name="高密度_含异常点")
    print(f"    样本数: {raw.size}, 缺失值: {np.isnan(raw.y).sum()}, 异常点: 3 (故意注入)")

    # ===== 2. 智能异常点剔除 + 预处理 =====
    print("\n" + "=" * 70)
    print("[2] 智能异常点剔除（SMART 方法）")
    pre = DataPreprocessor(
        missing_method=MissingMethod.LINEAR,
        smoothing=SmoothingMethod.SAVITZKY_GOLAY,
        window=5,
        outlier_method=OutlierMethod.SMART,
        outlier_threshold=2.5,
        outlier_k=5,
    )
    processed = pre.fit_transform(raw)
    print(f"    {pre.report.summary()}")
    if pre.report.outlier_details:
        print("    检测到的异常点：")
        for d in pre.report.outlier_details:
            print(f"      idx={d.index} x={d.x:.4f} y={d.y:.4f} "
                  f"原因={d.reason} 评分={d.score:.4f}")

    # ===== 3. 自适应网格 =====
    print("\n" + "=" * 70)
    print("[3] 自适应网格生成")
    x_grid = generate_adaptive_grid(
        processed, base_density=100, refine_density=300, curvature_threshold=0.15
    )
    print(f"    自适应网格点数: {x_grid.size}")

    # ===== 4. 拉格朗日插值（分段重心插值）=====
    print("\n" + "=" * 70)
    print("[4] 拉格朗日插值（分段重心插值，防 Runge 震荡）")
    lag = LagrangeInterpolator(
        max_degree=4, use_piecewise=True, window_size=5, boundary_mode="clamp"
    )
    lag_res: LagrangeResult = lag.interpolate(processed, x_grid)
    print(f"    分段重心插值，窗口={lag.window_size}, 边界模式=clamp")

    # ===== 5. 三种样条对比 =====
    print("\n" + "=" * 70)
    print("[5] 三种样条插值对比")
    spl_cubic = SplineInterpolator(
        spline_type=SplineType.CUBIC,
        bc_type=SplineBC.NOT_A_KNOT,
        extrap_mode=ExtrapMode.CLAMP,
    )
    spl_cubic_res = spl_cubic.interpolate(processed, x_grid)
    print(f"    精确样条: 分段={spl_cubic_res.segments}")

    spl_smooth = SplineInterpolator(
        spline_type=SplineType.SMOOTHING,
        smoothing_factor=None,
        extrap_mode=ExtrapMode.CLAMP,
    )
    spl_smooth_res = spl_smooth.interpolate(processed, x_grid)
    print(f"    平滑样条: s={spl_smooth_res.smoothing_factor:.2f} 分段={spl_smooth_res.segments}")

    spl_mono = SplineInterpolator(
        spline_type=SplineType.MONOTONE,
        extrap_mode=ExtrapMode.CLAMP,
    )
    spl_mono_res = spl_mono.interpolate(processed, x_grid)
    print(f"    单调样条 (PCHIP): 分段={spl_mono_res.segments}")

    # ===== 6. 多项式拟合（CV + Ridge）=====
    print("\n" + "=" * 70)
    print("[6] 多项式拟合（CV 自动选阶 + Ridge 正则化）")
    fitter = PolynomialFitter(
        degree=8, auto_select=True, max_degree=12,
        cv_folds=-1, auto_ridge=True,
        ridge_grid=[0.0, 1e-5, 1e-4, 1e-3, 1e-2, 1e-1, 1.0],
    )
    fitter.fit(processed)
    poly_res: PolynomialResult = fitter.result_  # type: ignore[assignment]
    poly_y_grid = fitter.predict(x_grid)
    print(f"    自动选阶={poly_res.degree}, R²={poly_res.r2:.6f}, "
          f"RMSE={poly_res.rmse:.6f}, CV-RMSE={poly_res.cv_rmse:.6f}, "
          f"Ridge={poly_res.ridge:.2e}")
    print(f"    方程={poly_res.equation}")

    # ===== 7. 多算法对比 =====
    print("\n" + "=" * 70)
    print("[7] 多算法对比（7 种算法自动评分）")
    comparator = AlgorithmComparator(
        algorithms=[
            "lagrange_piecewise", "lagrange_global",
            "spline_cubic", "spline_smoothing", "spline_monotone",
            "polynomial_cv", "polynomial_fixed",
        ],
        grid_density=200,
    )
    compare_report = comparator.compare(processed)
    print(compare_report.summary())

    # ===== 8. 趋势预测 =====
    print("\n" + "=" * 70)
    print("[8] 趋势预测（线性模型 + Bootstrap 置信区间）")
    tp = TrendPredictor(
        model_type=TrendModel.LINEAR,
        confidence=0.95,
        n_bootstrap=1000,
    )
    fit_ds = Dataset(x=processed.x, y=poly_res.fitted_y, name="fit_curve")
    tp.fit(fit_ds)
    trend_res: TrendPrediction = tp.forecast(steps=25)
    print(f"    方向={trend_res.direction}, 斜率={trend_res.slope:.6f}")
    print(f"    预测步: x={trend_res.x_forecast[0]:.3f} -> x={trend_res.x_forecast[-1]:.3f}")
    print(f"    最终预测值: {trend_res.y_forecast[-1]:.4f} "
          f"[{trend_res.y_lower[-1]:.4f}, {trend_res.y_upper[-1]:.4f}] "
          f"(95% CI)")

    # ===== 9. 误差分析 =====
    print("\n" + "=" * 70)
    print("[9] 误差分析报表")
    analyzer = ErrorAnalyzer()
    analyzer.build_report(
        "分段拉格朗日", "Lagrange-Piecewise",
        processed.y, lag.predict(processed.x),
        equation=lag_res.equation, degree=lag_res.degree,
        n_samples=processed.size,
        extra={"piecewise": True, "window": 5},
    )
    analyzer.build_report(
        "精确样条", "Spline-Cubic",
        processed.y, spl_cubic.predict(processed.x),
        n_samples=processed.size,
        extra={"bc_type": "not-a-knot"},
    )
    analyzer.build_report(
        "平滑样条", "Spline-Smoothing",
        processed.y, spl_smooth.predict(processed.x),
        n_samples=processed.size,
        extra={"s": spl_smooth_res.smoothing_factor},
    )
    analyzer.build_report(
        "单调样条", "Spline-Monotone",
        processed.y, spl_mono.predict(processed.x),
        n_samples=processed.size,
    )
    analyzer.build_report(
        "多项式拟合(CV+Ridge)", "Polynomial-CV",
        processed.y, poly_res.fitted_y,
        equation=poly_res.equation, degree=poly_res.degree,
        n_samples=processed.size,
        extra={
            "r2": poly_res.r2, "rmse": poly_res.rmse,
            "cv_rmse": poly_res.cv_rmse,
            "condition": poly_res.condition_number,
            "ridge": poly_res.ridge,
        },
    )

    txt_path = os.path.join(OUTPUT_DIR, "error_report.txt")
    json_path = os.path.join(OUTPUT_DIR, "error_report.json")
    analyzer.save_text(txt_path)
    analyzer.save_json(json_path)
    print(f"    报表已保存: {txt_path}, {json_path}")

    # ===== 10. 可视化 =====
    print("\n" + "=" * 70)
    print("[10] 可视化")
    fig, axes = plt.subplots(3, 2, figsize=(16, 18))

    # 左上图：异常点检测结果
    ax = axes[0, 0]
    ax.scatter(processed.x, processed.y, color="black", s=30,
               label="清洗后样本", zorder=5)
    if pre.report.outlier_details:
        ox = [d.x for d in pre.report.outlier_details]
        oy = [d.y for d in pre.report.outlier_details]
        ax.scatter(ox, oy, color="red", s=80, marker="x", linewidths=2,
                   label=f"检测到的异常点({len(ox)}个)", zorder=6)
    ax.set_title("智能异常点剔除（SMART 方法）")
    ax.set_xlabel("x")
    ax.set_ylabel("y")
    ax.legend(fontsize=8)
    ax.grid(alpha=0.3)

    # 右上图：三种样条对比
    ax = axes[0, 1]
    ax.scatter(processed.x, processed.y, color="black", s=20,
               label="样本", zorder=5)
    ax.plot(x_grid, spl_cubic_res.interpolated_y, "b-", linewidth=1.5,
            label=f"精确样条")
    ax.plot(x_grid, spl_smooth_res.interpolated_y, "g--", linewidth=1.5,
            label=f"平滑样条 (s={spl_smooth_res.smoothing_factor:.1f})")
    ax.plot(x_grid, spl_mono_res.interpolated_y, "r-.", linewidth=1.5,
            label="单调样条 (PCHIP)")
    ax.set_title("三种样条插值对比")
    ax.set_xlabel("x")
    ax.set_ylabel("y")
    ax.legend(fontsize=8)
    ax.grid(alpha=0.3)

    # 左中图：所有方法对比
    ax = axes[1, 0]
    ax.scatter(processed.x, processed.y, color="black", s=20,
               label="样本", zorder=5)
    ax.plot(x_grid, lag_res.interpolated_y, "b--", linewidth=1.2,
            label=f"分段拉格朗日")
    ax.plot(x_grid, spl_smooth_res.interpolated_y, "g-.", linewidth=1.2,
            label=f"平滑样条")
    ax.plot(x_grid, poly_y_grid, "m-", linewidth=2,
            label=f"多项式 (deg={poly_res.degree})")
    ax.set_title("拟合方法综合对比")
    ax.set_xlabel("x")
    ax.set_ylabel("y")
    ax.legend(fontsize=8)
    ax.grid(alpha=0.3)

    # 右中图：多算法对比柱状图
    ax = axes[1, 1]
    names = [s.name for s in compare_report.scores]
    r2_vals = [s.metrics.r2 for s in compare_report.scores]
    colors = ["#2ecc71" if s.rank == 1 else "#3498db" for s in compare_report.scores]
    bars = ax.barh(names, r2_vals, color=colors, edgecolor="white")
    ax.set_xlabel("R²")
    ax.set_title("多算法 R² 对比（绿色=最优）")
    ax.set_xlim(max(-0.2, min(r2_vals) - 0.1), 1.05)
    for bar, r2 in zip(bars, r2_vals):
        ax.text(bar.get_width() + 0.01, bar.get_y() + bar.get_height() / 2,
                f"{r2:.4f}", va="center", fontsize=8)

    # 左下图：趋势预测
    ax = axes[2, 0]
    ax.scatter(processed.x, processed.y, color="black", s=20,
               label="原始数据", zorder=5)
    ax.plot(processed.x, poly_res.fitted_y, "b-", linewidth=2,
            label="多项式拟合")
    ax.plot(trend_res.x_forecast, trend_res.y_forecast, "m-", linewidth=2,
            label=f"趋势预测 ({trend_res.direction})")
    ax.fill_between(
        trend_res.x_forecast, trend_res.y_lower, trend_res.y_upper,
        alpha=0.2, color="purple", label="95% 置信区间"
    )
    ax.axvline(x=processed.x[-1], color="gray", linestyle=":", alpha=0.5,
               label="数据边界")
    ax.set_title("趋势预测（含置信区间）")
    ax.set_xlabel("x")
    ax.set_ylabel("y")
    ax.legend(fontsize=7)
    ax.grid(alpha=0.3)

    # 右下图：真实值 vs 预测值
    ax = axes[2, 1]
    ax.scatter(processed.y, poly_res.fitted_y, color="tab:blue", s=30,
               label=f"多项式 (R²={poly_res.r2:.3f})")
    ax.scatter(processed.y, lag.predict(processed.x), color="tab:orange",
               s=30, label="分段拉格朗日")
    ax.scatter(processed.y, spl_smooth.predict(processed.x),
               color="tab:green", s=30, label="平滑样条")
    lim_min = min(processed.y.min(), poly_res.fitted_y.min())
    lim_max = max(processed.y.max(), poly_res.fitted_y.max())
    ax.plot([lim_min, lim_max], [lim_min, lim_max], "k--", linewidth=1,
            label="y=ŷ")
    ax.set_title("真实值 vs 预测值")
    ax.set_xlabel("真实 y")
    ax.set_ylabel("预测 y")
    ax.legend(fontsize=7)
    ax.grid(alpha=0.3)

    plt.tight_layout()
    png_path = os.path.join(OUTPUT_DIR, "fit_visualization.png")
    plt.savefig(png_path, dpi=150)
    print(f"    可视化已保存: {png_path}")

    # ===== 11. 批量处理演示 =====
    print("\n" + "=" * 70)
    print("[11] 批量数据处理演示（3 个数据集）")
    datasets = [
        generate_demo(n=15, noise=0.2, missing_rate=0.0, seed=1, name="batch_1"),
        generate_demo(n=25, noise=0.4, missing_rate=0.08, seed=2, name="batch_2"),
        generate_demo(n=40, noise=0.3, missing_rate=0.0, seed=3, name="batch_3"),
    ]

    def pre_fn(ds: Dataset) -> Dataset:
        pp = DataPreprocessor(
            missing_method=MissingMethod.LINEAR,
            smoothing=SmoothingMethod.SAVITZKY_GOLAY,
            window=5,
            outlier_method=OutlierMethod.SMART,
            outlier_threshold=2.5,
        )
        return pp.fit_transform(ds)

    def fit_fn(ds: Dataset) -> BatchResult:
        ft = PolynomialFitter(
            degree=3, auto_select=True, max_degree=8,
            cv_folds=-1, auto_ridge=True,
        )
        ft.fit(ds)
        return BatchResult(
            name=ds.name, n_samples=ds.size,
            method=f"Polynomial(deg={ft.result_.degree})",
            equation=ft.result_.equation,
            metrics={"r2": round(ft.result_.r2, 4),
                     "rmse": round(ft.result_.rmse, 4),
                     "ridge": round(ft.result_.ridge, 6)},
        )

    bp = BatchProcessor(max_points_per_dataset=300)
    results = bp.process(datasets, pre_fn, fit_fn, verbose=True)
    print("\n" + bp.summary())

    print("\n" + analyzer.text_report())
    print("[完成] 示例运行结束。")


if __name__ == "__main__":
    main()
