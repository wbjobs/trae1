"""多算法对比模块

对同一数据集自动运行多种拟合/插值算法，综合评分并推荐最优算法。
支持自定义算法组合、权重配置。
"""

from __future__ import annotations

from dataclasses import dataclass, field
from typing import Any, Callable, Dict, List, Optional, Tuple

import numpy as np

from .dataio import Dataset, generate_grid
from .error_analysis import ErrorAnalyzer, ErrorMetrics
from .lagrange import LagrangeInterpolator
from .polynomial import PolynomialFitter
from .spline import ExtrapMode, SplineBC, SplineInterpolator, SplineType


@dataclass
class AlgorithmScore:
    """单个算法的评分结果"""
    name: str
    metrics: ErrorMetrics
    score: float
    rank: int
    details: Dict[str, Any] = field(default_factory=dict)


@dataclass
class ComparisonReport:
    """多算法对比报告"""
    dataset_name: str
    n_samples: int
    scores: List[AlgorithmScore]
    best_algorithm: str
    best_score: float
    weights: Dict[str, float]

    def summary(self) -> str:
        lines = [
            f"{'=' * 60}",
            f"多算法对比报告: {self.dataset_name}",
            f"样本数: {self.n_samples}",
            f"{'=' * 60}",
        ]
        for s in sorted(self.scores, key=lambda x: x.rank):
            m = s.metrics
            lines.append(
                f"  [#{s.rank}] {s.name:<24s} 得分={s.score:.4f}  "
                f"R²={m.r2:.4f}  RMSE={m.rmse:.4f}  MAE={m.mae:.4f}"
            )
        lines.append(f"\n  最优算法: {self.best_algorithm} (得分={self.best_score:.4f})")
        return "\n".join(lines)


_DEFAULT_WEIGHTS: Dict[str, float] = {
    "r2": 0.35,
    "rmse": 0.25,
    "mae": 0.15,
    "max_abs_error": 0.10,
    "bias": 0.10,
    "mean_abs_percent_error": 0.05,
}


class AlgorithmComparator:
    """多算法对比器

    对同一数据自动运行多种拟合/插值算法，按加权综合评分排名。

    Parameters
    ----------
    algorithms : Optional[List[str]]
        要对比的算法列表，可选值：
        ``"lagrange_piecewise"``、``"lagrange_global"``、
        ``"spline_cubic"``、``"spline_smoothing"``、``"spline_monotone"``、
        ``"polynomial_cv"``、``"polynomial_fixed"``。
        为 None 时使用全部算法。
    weights : Optional[Dict[str, float]]
        指标权重，键为 ErrorMetrics 字段名，值为权重。
        为 None 时使用默认权重。
    grid_density : int
        评估时的网格点密度
    """

    ALL_ALGORITHMS = [
        "lagrange_piecewise",
        "lagrange_global",
        "spline_cubic",
        "spline_smoothing",
        "spline_monotone",
        "polynomial_cv",
        "polynomial_fixed",
    ]

    def __init__(
        self,
        algorithms: Optional[List[str]] = None,
        weights: Optional[Dict[str, float]] = None,
        grid_density: int = 200,
    ) -> None:
        self.algorithms = algorithms or list(self.ALL_ALGORITHMS)
        self.weights = weights or dict(_DEFAULT_WEIGHTS)
        self.grid_density = grid_density
        self._last_report: Optional[ComparisonReport] = None

    @property
    def last_report(self) -> Optional[ComparisonReport]:
        return self._last_report

    # ---------- 主入口 ----------
    def compare(self, data: Dataset) -> ComparisonReport:
        """运行所有算法并返回对比报告"""
        data = data.sort()
        x_grid = generate_grid(data, density=self.grid_density)
        analyzer = ErrorAnalyzer()
        scores: List[AlgorithmScore] = []

        for alg_name in self.algorithms:
            try:
                y_pred = self._run_algorithm(alg_name, data, x_grid)
                metrics = analyzer.compute_metrics(data.y, y_pred)
                score = self._compute_score(metrics)
                scores.append(
                    AlgorithmScore(
                        name=alg_name,
                        metrics=metrics,
                        score=score,
                        rank=0,
                        details=self._get_details(alg_name, data),
                    )
                )
            except Exception as e:
                scores.append(
                    AlgorithmScore(
                        name=alg_name,
                        metrics=ErrorMetrics(
                            mae=np.inf, mse=np.inf, rmse=np.inf,
                            r2=-np.inf, max_abs_error=np.inf,
                            mean_abs_percent_error=np.inf, bias=np.inf,
                        ),
                        score=-np.inf,
                        rank=0,
                        details={"error": str(e)},
                    )
                )

        # 排名
        valid_scores = [s for s in scores if np.isfinite(s.score)]
        valid_scores.sort(key=lambda s: s.score, reverse=True)
        for i, s in enumerate(valid_scores):
            s.rank = i + 1
        invalid_scores = [s for s in scores if not np.isfinite(s.score)]
        for s in invalid_scores:
            s.rank = len(valid_scores) + 1

        all_scores = valid_scores + invalid_scores

        best = all_scores[0] if all_scores else None
        report = ComparisonReport(
            dataset_name=data.name,
            n_samples=data.size,
            scores=all_scores,
            best_algorithm=best.name if best else "N/A",
            best_score=best.score if best else 0.0,
            weights=self.weights,
        )
        self._last_report = report
        return report

    # ---------- 运行单个算法 ----------
    def _run_algorithm(
        self, name: str, data: Dataset, x_grid: np.ndarray
    ) -> np.ndarray:
        """运行指定算法，返回在原始数据点上的预测值"""

        if name == "lagrange_piecewise":
            interp = LagrangeInterpolator(
                max_degree=4, use_piecewise=True, window_size=5,
                boundary_mode="clamp",
            )
            interp.fit(data)
            return interp.predict(data.x)

        elif name == "lagrange_global":
            interp = LagrangeInterpolator(
                max_degree=min(data.size - 1, 8),
                use_piecewise=False, boundary_mode="clamp",
            )
            interp.fit(data)
            return interp.predict(data.x)

        elif name == "spline_cubic":
            spl = SplineInterpolator(
                spline_type=SplineType.CUBIC,
                bc_type=SplineBC.NOT_A_KNOT,
                extrap_mode=ExtrapMode.CLAMP,
            )
            spl.fit(data)
            return spl.predict(data.x)

        elif name == "spline_smoothing":
            spl = SplineInterpolator(
                spline_type=SplineType.SMOOTHING,
                smoothing_factor=None,
                extrap_mode=ExtrapMode.CLAMP,
            )
            spl.fit(data)
            return spl.predict(data.x)

        elif name == "spline_monotone":
            spl = SplineInterpolator(
                spline_type=SplineType.MONOTONE,
                extrap_mode=ExtrapMode.CLAMP,
            )
            spl.fit(data)
            return spl.predict(data.x)

        elif name == "polynomial_cv":
            ft = PolynomialFitter(
                degree=3, auto_select=True, max_degree=10,
                cv_folds=-1, auto_ridge=True,
            )
            ft.fit(data)
            return ft.result_.fitted_y

        elif name == "polynomial_fixed":
            ft = PolynomialFitter(degree=3, ridge=0.0)
            ft.fit(data)
            return ft.result_.fitted_y

        else:
            raise ValueError(f"未知算法: {name}")

    # ---------- 评分 ----------
    def _compute_score(self, metrics: ErrorMetrics) -> float:
        """综合评分：R² 越高越好，误差越低越好"""
        w = self.weights
        score = 0.0
        if "r2" in w:
            score += w["r2"] * max(0.0, min(1.0, metrics.r2))
        if "rmse" in w:
            score -= w["rmse"] * _normalize(metrics.rmse)
        if "mae" in w:
            score -= w["mae"] * _normalize(metrics.mae)
        if "max_abs_error" in w:
            score -= w["max_abs_error"] * _normalize(metrics.max_abs_error)
        if "bias" in w:
            score -= w["bias"] * _normalize(abs(metrics.bias))
        if "mean_abs_percent_error" in w:
            score -= w["mean_abs_percent_error"] * min(1.0, metrics.mean_abs_percent_error / 100.0)
        return score

    def _get_details(self, name: str, data: Dataset) -> Dict[str, Any]:
        return {"algorithm": name}


def _normalize(value: float) -> float:
    """将误差归一化到 [0, 1] 区间用于评分"""
    if not np.isfinite(value):
        return 1.0
    return 1.0 - np.exp(-value)
