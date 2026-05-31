"""离散数据曲线拟合与插值计算系统 —— 主程序入口

功能：
1. 加载数据（CSV 或内置演示数据，支持批量）
2. 预处理：缺失值填充、平滑、智能异常点剔除
3. 拉格朗日插值（分段，防 Runge 震荡，边界保护）
4. 三次样条 / 平滑样条 / 单调样条插值
5. 多项式拟合（交叉验证自动选阶，条件数约束，Ridge 正则化）
6. 多算法对比与推荐
7. 趋势预测（含置信区间）
8. 误差分析与文本/JSON 报表导出
9. 自适应网格 + 可视化对比
"""

from __future__ import annotations

import argparse
import os
from typing import List

import matplotlib
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
    PolynomialFitter,
    SplineBC,
    SplineInterpolator,
    SplineType,
    TrendModel,
    TrendPredictor,
    downsample,
    generate_adaptive_grid,
    generate_demo,
    generate_grid,
    load_csv,
)
from curvefit.dataio import Dataset
from curvefit.lagrange import LagrangeResult
from curvefit.polynomial import PolynomialResult
from curvefit.preprocessing import (
    MissingMethod,
    OutlierMethod,
    SmoothingMethod,
)
from curvefit.spline import SplineResult
from curvefit.trend import TrendPrediction


def setup_argparser() -> argparse.ArgumentParser:
    p = argparse.ArgumentParser(
        description="离散数据曲线拟合与插值计算系统",
        formatter_class=argparse.ArgumentDefaultsHelpFormatter,
    )
    p.add_argument(
        "--csv", type=str, default=None, help="输入 CSV 文件路径（含 x,y 列）"
    )
    p.add_argument(
        "--csv-dir", type=str, default=None,
        help="批量输入目录，读取目录下所有 CSV 文件"
    )

    # 预处理参数
    p.add_argument(
        "--smooth", type=str, default="savgol",
        choices=["savgol", "moving_average", "none"],
        help="平滑方法",
    )
    p.add_argument(
        "--window", type=int, default=5, help="平滑窗口（奇数）"
    )
    p.add_argument(
        "--missing", type=str, default="linear",
        choices=["linear", "nearest", "cubic", "drop"],
        help="缺失值处理方式",
    )
    p.add_argument(
        "--outlier", type=str, default="smart",
        choices=["none", "iqr", "zscore", "smart", "residual"],
        help="异常值剔除方法",
    )
    p.add_argument(
        "--outlier-threshold", type=float, default=2.5, help="异常值阈值"
    )
    p.add_argument(
        "--outlier-k", type=int, default=5,
        help="智能异常点检测的邻域窗口大小"
    )

    # 多项式拟合参数
    p.add_argument(
        "--degree", type=int, default=3,
        help="多项式拟合阶数（若启用 auto_select 则作为搜索上限）"
    )
    p.add_argument(
        "--auto-select", action="store_true",
        help="根据交叉验证自动选择最优多项式阶数"
    )
    p.add_argument(
        "--ridge", type=float, default=0.0,
        help="多项式拟合 L2 正则化系数"
    )
    p.add_argument(
        "--auto-ridge", action="store_true",
        help="自动搜索最优 ridge 系数"
    )
    p.add_argument(
        "--max-cond", type=float, default=1e10,
        help="范德蒙德矩阵最大允许条件数"
    )
    p.add_argument(
        "--cv-folds", type=int, default=-1,
        help="交叉验证折数，-1 表示留一法"
    )

    # 样条参数
    p.add_argument(
        "--spline-type", type=str, default="cubic",
        choices=["cubic", "smoothing", "monotone"],
        help="样条类型",
    )
    p.add_argument(
        "--spline-bc", type=str, default="not-a-knot",
        choices=["not-a-knot", "natural", "clamped"],
        help="三次样条边界条件",
    )
    p.add_argument(
        "--spline-s", type=float, default=None,
        help="平滑样条的平滑因子 s（None=自动）"
    )
    p.add_argument(
        "--spline-extrap", type=str, default="clamp",
        choices=["clamp", "linear", "extrapolate", "nan"],
        help="样条外推模式",
    )

    # 拉格朗日参数
    p.add_argument(
        "--lag-window", type=int, default=5,
        help="拉格朗日分段窗口大小（3-7）"
    )
    p.add_argument(
        "--lag-boundary", type=str, default="clamp",
        choices=["clamp", "extrapolate", "linear"],
        help="拉格朗日边界处理模式",
    )

    # 多算法对比
    p.add_argument(
        "--compare", action="store_true",
        help="运行多算法对比，自动推荐最优算法"
    )

    # 趋势预测
    p.add_argument(
        "--forecast", action="store_true",
        help="启用趋势预测"
    )
    p.add_argument(
        "--forecast-steps", type=int, default=20,
        help="趋势预测外推步数"
    )
    p.add_argument(
        "--forecast-model", type=str, default="linear",
        choices=["linear", "polynomial", "exponential"],
        help="趋势预测模型类型"
    )
    p.add_argument(
        "--forecast-confidence", type=float, default=0.95,
        help="趋势预测置信水平"
    )

    # 网格与性能
    p.add_argument(
        "--grid-density", type=int, default=200, help="插值网格点密度"
    )
    p.add_argument(
        "--adaptive-grid", action="store_true",
        help="使用自适应网格（曲率大的区域加密）"
    )
    p.add_argument(
        "--max-points", type=int, default=2000,
        help="单个数据集最大保留点数（降采样上限）"
    )
    p.add_argument(
        "--output-dir", type=str, default="output",
        help="报表与图像输出目录",
    )
    p.add_argument(
        "--no-plot", action="store_true", help="关闭可视化绘图"
    )
    return p


def _load_datasets(args: argparse.Namespace) -> List[Dataset]:
    datasets: List[Dataset] = []
    if args.csv_dir:
        if not os.path.isdir(args.csv_dir):
            raise FileNotFoundError(f"目录不存在: {args.csv_dir}")
        for fname in sorted(os.listdir(args.csv_dir)):
            if fname.lower().endswith(".csv"):
                fpath = os.path.join(args.csv_dir, fname)
                datasets.append(load_csv(fpath))
                print(f"[数据] 已加载 {fpath}，共 {datasets[-1].size} 个样本")
    if args.csv:
        ds = load_csv(args.csv)
        datasets.append(ds)
        print(f"[数据] 已加载 {args.csv}，共 {ds.size} 个样本")
    if not datasets:
        raw = generate_demo(n=20, noise=0.3, missing_rate=0.1, seed=42)
        datasets.append(raw)
        print(f"[数据] 使用内置演示数据，共 {raw.size} 个样本（含缺失值）")
    return datasets


def run(args: argparse.Namespace) -> None:
    datasets = _load_datasets(args)
    os.makedirs(args.output_dir, exist_ok=True)

    analyzer = ErrorAnalyzer()
    all_plots: list = []

    for ds_idx, raw in enumerate(datasets):
        print(f"\n{'=' * 60}")
        print(f"数据集 {ds_idx + 1}/{len(datasets)}: {raw.name}")
        print(f"{'=' * 60}")

        # 降采样
        if raw.size > args.max_points:
            raw = downsample(raw, args.max_points)
            print(f"[降采样] 数据量过大，降至 {raw.size} 个点")

        # 预处理
        preprocessor = DataPreprocessor(
            missing_method=MissingMethod(args.missing),
            smoothing=SmoothingMethod(args.smooth),
            window=args.window,
            outlier_method=OutlierMethod(args.outlier),
            outlier_threshold=args.outlier_threshold,
            outlier_k=args.outlier_k,
        )
        processed = preprocessor.fit_transform(raw)
        print(preprocessor.report.summary())
        if preprocessor.report.outlier_details:
            print(f"[异常点详情] 共检测到 {len(preprocessor.report.outlier_details)} 个异常点：")
            for d in preprocessor.report.outlier_details[:5]:
                print(f"  idx={d.index} x={d.x:.4f} y={d.y:.4f} "
                      f"原因={d.reason} 评分={d.score:.4f}")
            if len(preprocessor.report.outlier_details) > 5:
                print(f"  ... 其余 {len(preprocessor.report.outlier_details) - 5} 个省略")

        # 网格生成
        if args.adaptive_grid:
            x_grid = generate_adaptive_grid(
                processed, base_density=args.grid_density // 2,
                refine_density=args.grid_density,
            )
        else:
            x_grid = generate_grid(processed, density=args.grid_density)
        print(f"[网格] 采样点 {x_grid.size} 个"
              + ("（自适应）" if args.adaptive_grid else ""))

        # 拉格朗日
        lag = LagrangeInterpolator(
            max_degree=min(processed.size - 1, 8),
            use_piecewise=True,
            window_size=args.lag_window,
            boundary_mode=args.lag_boundary,
        )
        lag_res: LagrangeResult = lag.interpolate(processed, x_grid)

        # 样条
        spl = SplineInterpolator(
            spline_type=SplineType(args.spline_type),
            bc_type=SplineBC(args.spline_bc),
            smoothing_factor=args.spline_s,
            extrap_mode=ExtrapMode(args.spline_extrap),
        )
        spl_res: SplineResult = spl.interpolate(processed, x_grid)

        # 多项式拟合
        fitter = PolynomialFitter(
            degree=args.degree,
            ridge=args.ridge,
            auto_select=args.auto_select,
            max_degree=max(args.degree, 10),
            max_cond=args.max_cond,
            cv_folds=args.cv_folds,
            auto_ridge=args.auto_ridge,
        )
        fitter.fit(processed)
        poly_res: PolynomialResult = fitter.result_  # type: ignore[assignment]
        poly_y_grid = fitter.predict(x_grid)

        print(f"[拉格朗日] 阶数={lag_res.degree}"
              + ("（分段）" if lag_res.piecewise else "（全局）")
              + f" 边界={args.lag_boundary}")
        print(f"[样条] 类型={spl_res.spline_type} 边界={spl_res.bc_type}"
              + f" 外推={spl_res.extrap_mode} 分段={spl_res.segments}")
        print(f"[多项式] 阶数={poly_res.degree} R²={poly_res.r2:.4f}"
              f" RMSE={poly_res.rmse:.4f} CV-RMSE={poly_res.cv_rmse:.4f}"
              f" Ridge={poly_res.ridge:.2e}")
        print(f"[多项式] 方程 = {poly_res.equation}")

        # 误差分析
        analyzer.build_report(
            name=f"{raw.name} - 拉格朗日插值",
            method="Lagrange",
            y_true=processed.y,
            y_pred=lag.predict(processed.x),
            equation=lag_res.equation,
            degree=lag_res.degree,
            n_samples=processed.size,
            extra={"piecewise": lag_res.piecewise,
                   "boundary_mode": args.lag_boundary},
        )
        analyzer.build_report(
            name=f"{raw.name} - 样条插值",
            method=f"Spline({spl_res.spline_type})",
            y_true=processed.y,
            y_pred=spl.predict(processed.x),
            equation="分段多项式（见详情）",
            n_samples=processed.size,
            extra={"bc_type": spl_res.bc_type,
                   "spline_type": spl_res.spline_type,
                   "extrap_mode": spl_res.extrap_mode,
                   "segments": spl_res.segments},
        )
        analyzer.build_report(
            name=f"{raw.name} - 多项式拟合",
            method="Polynomial",
            y_true=processed.y,
            y_pred=poly_res.fitted_y,
            equation=poly_res.equation,
            degree=poly_res.degree,
            n_samples=processed.size,
            extra={
                "r2": poly_res.r2,
                "rmse": poly_res.rmse,
                "cv_rmse": poly_res.cv_rmse,
                "condition_number": poly_res.condition_number,
                "ridge": poly_res.ridge,
                "used_cv": poly_res.used_cv,
            },
        )

        # 多算法对比
        compare_report = None
        if args.compare:
            print("\n[多算法对比] 运行中...")
            comparator = AlgorithmComparator(grid_density=args.grid_density)
            compare_report = comparator.compare(processed)
            print(compare_report.summary())

        # 趋势预测
        trend_result = None
        if args.forecast:
            print(f"\n[趋势预测] 模型={args.forecast_model}"
                  f" 置信度={args.forecast_confidence}"
                  f" 步数={args.forecast_steps}")
            tp = TrendPredictor(
                model_type=TrendModel(args.forecast_model),
                confidence=args.forecast_confidence,
                poly_degree=args.degree,
            )
            # 用多项式拟合的结果作为趋势拟合输入
            fit_dataset = Dataset(x=processed.x, y=poly_res.fitted_y, name="fit_curve")
            tp.fit(fit_dataset)
            trend_result = tp.forecast(steps=args.forecast_steps)
            print(f"  方向: {trend_result.direction}, 斜率: {trend_result.slope:.6f}")
            print(f"  预测区间: [{trend_result.y_lower[-1]:.4f}, {trend_result.y_upper[-1]:.4f}]"
                  f" @ x={trend_result.x_forecast[-1]:.4f}")

        # 收集绘图数据
        all_plots.append((
            raw.name, processed, x_grid,
            lag_res, spl_res, poly_y_grid, poly_res,
            compare_report, trend_result,
        ))

    # 输出报表
    txt_path = os.path.join(args.output_dir, "error_report.txt")
    json_path = os.path.join(args.output_dir, "error_report.json")
    analyzer.save_text(txt_path)
    analyzer.save_json(json_path)
    print(f"\n[报表] 已保存: {txt_path}")
    print(f"[报表] 已保存: {json_path}")

    print("\n" + analyzer.text_report())

    # 可视化
    if not args.no_plot and all_plots:
        _plot_results(all_plots, args)


def _plot_results(all_plots, args):
    n_ds = len(all_plots)
    ncols = 2
    nrows = (n_ds * 2 + ncols - 1) // ncols
    fig, axes = plt.subplots(nrows, ncols, figsize=(7 * ncols, 5 * nrows))
    axes = np.atleast_1d(axes).flatten()

    for plot_idx, (name, processed, x_grid,
                   lag_res, spl_res, poly_y_grid, poly_res,
                   compare_report, trend_result) in enumerate(all_plots):
        ax1 = axes[plot_idx * 2]
        ax2 = axes[plot_idx * 2 + 1]

        ax1.scatter(processed.x, processed.y, color="black", s=40,
                    label="预处理后样本", zorder=5)
        ax1.plot(x_grid, lag_res.interpolated_y, linestyle="--",
                 linewidth=1.6,
                 label=f"拉格朗日 (deg={lag_res.degree}"
                       + (" 分段)" if lag_res.piecewise else ")"))
        ax1.plot(x_grid, spl_res.interpolated_y, linestyle="-.",
                 linewidth=1.6,
                 label=f"样条 ({spl_res.spline_type})")
        ax1.plot(x_grid, poly_y_grid, linestyle="-", linewidth=2.0,
                 label=f"多项式 (deg={poly_res.degree})")

        # 趋势预测区间
        if trend_result is not None:
            tp = trend_result
            ax1.fill_between(
                tp.x_forecast, tp.y_lower, tp.y_upper,
                alpha=0.2, color="purple", label="预测区间"
            )
            ax1.plot(tp.x_forecast, tp.y_forecast, "m-", linewidth=2,
                     label=f"趋势预测 ({tp.model_type})")

        ax1.set_title(f"{name} - 拟合与插值结果对比")
        ax1.set_xlabel("x")
        ax1.set_ylabel("y")
        ax1.legend(fontsize=8)
        ax1.grid(alpha=0.3)

        ax2.scatter(processed.y, poly_res.fitted_y, color="tab:blue",
                    s=40, label=f"多项式 (R²={poly_res.r2:.3f})")
        ax2.scatter(processed.y, lag.predict(processed.x),
                    color="tab:orange", s=40, label="拉格朗日")
        ax2.scatter(processed.y, spl.predict(processed.x),
                    color="tab:green", s=40, label="样条")
        lim_min = min(processed.y.min(), poly_res.fitted_y.min())
        lim_max = max(processed.y.max(), poly_res.fitted_y.max())
        ax2.plot([lim_min, lim_max], [lim_min, lim_max],
                 "k--", linewidth=1, label="y=ŷ")
        ax2.set_title(f"{name} - 真实值 vs 预测值")
        ax2.set_xlabel("真实 y")
        ax2.set_ylabel("预测 y")
        ax2.legend(fontsize=8)
        ax2.grid(alpha=0.3)

    for idx in range(n_ds * 2, len(axes)):
        axes[idx].set_visible(False)

    plt.tight_layout()
    png_path = os.path.join(args.output_dir, "fit_visualization.png")
    plt.savefig(png_path, dpi=150)
    print(f"[图像] 已保存: {png_path}")
    plt.show()


def main() -> None:
    args = setup_argparser().parse_args()
    run(args)


if __name__ == "__main__":
    main()
