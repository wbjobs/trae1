"""样条插值模块（三次样条 / 平滑样条 / 单调样条）

提供多种样条插值方式：
- **CubicSpline**：精确插值样条，经过所有数据点
- **SmoothingSpline**：平滑样条，允许数据有噪声，不强制过所有点
- **MonotoneSpline**：单调保形样条，保持数据单调性，不出现伪极值

均支持边界外推模式防止边界失真。
"""

from __future__ import annotations

from dataclasses import dataclass, field
from enum import Enum
from typing import List, Optional, Tuple

import numpy as np
from scipy.interpolate import CubicSpline, PchipInterpolator, UnivariateSpline

from .dataio import Dataset


class SplineBC(str, Enum):
    NOT_A_KNOT = "not-a-knot"
    NATURAL = "natural"
    CLAMPED = "clamped"


class SplineType(str, Enum):
    CUBIC = "cubic"
    SMOOTHING = "smoothing"
    MONOTONE = "monotone"


class ExtrapMode(str, Enum):
    CLAMP = "clamp"
    LINEAR = "linear"
    EXTRAPOLATE = "extrapolate"
    NAN = "nan"


@dataclass
class SplineResult:
    """样条插值结果"""

    interpolated_x: np.ndarray
    interpolated_y: np.ndarray
    bc_type: str
    extrap_mode: str
    segments: int
    spline_type: str = "cubic"
    smoothing_factor: float = 0.0
    coefficients: List[np.ndarray] = field(default_factory=list)


class SplineInterpolator:
    """样条插值器

    Parameters
    ----------
    spline_type : SplineType
        样条类型：
        - ``cubic``: 精确三次样条
        - ``smoothing``: 平滑样条（适合含噪声数据）
        - ``monotone``: 单调保形样条（PCHIP）
    bc_type : SplineBC
        边界条件类型（仅 cubic 类型有效）
    smoothing_factor : float
        平滑因子 s（仅 smoothing 类型有效）。
        s=0 时等价于插值样条；s 越大越平滑。
        若设为 None，则使用 scipy 默认值（s=len(x) - sqrt(2*len(x))）。
    extrap_mode : ExtrapMode
        数据范围外的处理方式：
        - ``clamp``: 返回边界值（默认，防止边界失真）
        - ``linear``: 线性外推
        - ``extrapolate``: 用样条外推（可能失真）
        - ``nan``: 返回 NaN
    """

    def __init__(
        self,
        spline_type: SplineType = SplineType.CUBIC,
        bc_type: SplineBC = SplineBC.NOT_A_KNOT,
        smoothing_factor: Optional[float] = None,
        extrap_mode: ExtrapMode = ExtrapMode.CLAMP,
    ) -> None:
        self.spline_type = spline_type
        self.bc_type = bc_type
        self.smoothing_factor = smoothing_factor
        self.extrap_mode = extrap_mode
        self._spline = None
        self._x_min: float = 0.0
        self._x_max: float = 0.0
        self._y_left: float = 0.0
        self._y_right: float = 0.0
        self._slope_left: float = 0.0
        self._slope_right: float = 0.0
        self.result_: Optional[SplineResult] = None

    # ---------- 核心 ----------
    def fit(self, data: Dataset) -> "SplineInterpolator":
        if data.size < 4:
            raise ValueError("样条插值至少需要 4 个样本点")
        data = data.sort()
        self._x_min = float(data.x[0])
        self._x_max = float(data.x[-1])

        if self.spline_type == SplineType.CUBIC:
            self._fit_cubic(data)
        elif self.spline_type == SplineType.SMOOTHING:
            self._fit_smoothing(data)
        elif self.spline_type == SplineType.MONOTONE:
            self._fit_monotone(data)
        else:
            raise ValueError(f"未知样条类型: {self.spline_type}")

        self._compute_boundary()
        return self

    def predict(self, x: np.ndarray) -> np.ndarray:
        if self._spline is None:
            raise RuntimeError("请先调用 fit 方法")
        x = np.asarray(x, dtype=float)
        y = np.asarray(self._spline(x), dtype=float)

        if self.extrap_mode == ExtrapMode.CLAMP:
            y = self._apply_clamp(x, y)
        elif self.extrap_mode == ExtrapMode.LINEAR:
            y = self._apply_linear(x, y)
        elif self.extrap_mode == ExtrapMode.NAN:
            y = self._apply_nan(x, y)
        return y

    def interpolate(self, data: Dataset, x_new: np.ndarray) -> SplineResult:
        self.fit(data)
        y_new = self.predict(x_new)
        coeffs: List[np.ndarray] = []
        segments = 0
        if self.spline_type == SplineType.CUBIC and hasattr(self._spline, "c"):
            coeffs = [c for c in self._spline.c]
            segments = self._spline.c.shape[1]
        elif self.spline_type == SplineType.SMOOTHING and hasattr(self._spline, "get_knots"):
            segments = len(self._spline.get_knots())
        else:
            segments = data.size - 1

        self.result_ = SplineResult(
            interpolated_x=np.asarray(x_new, dtype=float),
            interpolated_y=y_new,
            bc_type=self.bc_type.value,
            extrap_mode=self.extrap_mode.value,
            segments=segments,
            spline_type=self.spline_type.value,
            smoothing_factor=float(self.smoothing_factor or 0.0),
            coefficients=coeffs,
        )
        return self.result_

    # ---------- 拟合方法 ----------
    def _fit_cubic(self, data: Dataset) -> None:
        extrap = self.extrap_mode.value
        if extrap == "clamp":
            scipy_extrap = False
        elif extrap == "nan":
            scipy_extrap = np.nan
        else:
            scipy_extrap = True
        self._spline = CubicSpline(
            data.x, data.y, bc_type=self.bc_type.value, extrapolate=scipy_extrap
        )

    def _fit_smoothing(self, data: Dataset) -> None:
        s = self.smoothing_factor
        if s is None:
            s = data.size - np.sqrt(2.0 * data.size)
        s = max(s, 1e-10)
        self._spline = UnivariateSpline(
            data.x, data.y, s=s, k=3, ext="const"
        )
        self.smoothing_factor = float(s)

    def _fit_monotone(self, data: Dataset) -> None:
        self._spline = PchipInterpolator(data.x, data.y, extrapolate=False)

    def _compute_boundary(self) -> None:
        """预计算边界值和斜率，用于外推保护"""
        h = (self._x_max - self._x_min) * 0.01
        self._y_left = float(self._spline(self._x_min))
        self._y_right = float(self._spline(self._x_max))
        self._slope_left = (
            float(self._spline(self._x_min + h)) - self._y_left
        ) / h
        self._slope_right = (
            self._y_right - float(self._spline(self._x_max - h))
        ) / h

    # ---------- 外推保护 ----------
    def _apply_clamp(self, x: np.ndarray, y: np.ndarray) -> np.ndarray:
        y = y.copy()
        y[x < self._x_min] = self._y_left
        y[x > self._x_max] = self._y_right
        return y

    def _apply_linear(self, x: np.ndarray, y: np.ndarray) -> np.ndarray:
        y = y.copy()
        mask_l = x < self._x_min
        mask_r = x > self._x_max
        if np.any(mask_l):
            y[mask_l] = self._y_left + self._slope_left * (x[mask_l] - self._x_min)
        if np.any(mask_r):
            y[mask_r] = self._y_right + self._slope_right * (x[mask_r] - self._x_max)
        return y

    def _apply_nan(self, x: np.ndarray, y: np.ndarray) -> np.ndarray:
        y = y.copy()
        y[x < self._x_min] = np.nan
        y[x > self._x_max] = np.nan
        return y

    # ---------- 报告 ----------
    def piecewise_equations(self, x_knots: np.ndarray) -> List[str]:
        """生成各区间的分段多项式表达式（仅 cubic 类型可用）"""
        if self.spline_type != SplineType.CUBIC or self._spline is None:
            return ["(非 cubic 样条，无显式分段多项式)"]
        eqs: List[str] = []
        c = self._spline.c
        xs = x_knots
        for i in range(c.shape[1]):
            a3, a2, a1, a0 = c[0, i], c[1, i], c[2, i], c[3, i]
            x0 = xs[i]
            x1 = xs[i + 1]
            expr = (
                f"S_{i}(x) = {a3:.4f}*(x-{x0:.4f})^3 + "
                f"{a2:.4f}*(x-{x0:.4f})^2 + "
                f"{a1:.4f}*(x-{x0:.4f}) + {a0:.4f}"
            )
            eqs.append(f"[{x0:.4f}, {x1:.4f}]: {expr}")
        return eqs

    def estimate_residuals(self, data: Dataset) -> Tuple[np.ndarray, float]:
        """计算平滑样条的残差（仅 smoothing 类型有意义）"""
        if self._spline is None:
            raise RuntimeError("请先调用 fit 方法")
        y_pred = np.asarray(self._spline(data.x), dtype=float)
        residuals = data.y - y_pred
        rss = float(np.sum(residuals**2))
        return residuals, rss
