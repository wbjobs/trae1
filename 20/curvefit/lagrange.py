"""拉格朗日插值模块

提供全局拉格朗日与分段拉格朗日两种插值方式，支持边界保护以避免
Runge 震荡与边界失真。分段模式使用重心插值 (Barycentric) 保证
数值稳定性。
"""

from __future__ import annotations

from dataclasses import dataclass, field
from typing import List, Optional

import numpy as np
from scipy.interpolate import BarycentricInterpolator, lagrange

from .dataio import Dataset


_COEFF_MAX = 1e6


@dataclass
class LagrangeResult:
    """拉格朗日插值结果

    Attributes:
        polynomial: 插值多项式系数（高次到低次），若为分段则为空
        interpolated_x: 插值 x 值
        interpolated_y: 插值 y 值
        degree: 插值多项式次数
        piecewise: 是否为分段插值
        piecewise_polys: 各段多项式系数列表（仅分段模式）
        piecewise_breaks: 各段断点 x 值（仅分段模式）
    """

    polynomial: np.ndarray
    interpolated_x: np.ndarray
    interpolated_y: np.ndarray
    degree: int
    piecewise: bool = False
    piecewise_polys: List[np.ndarray] = field(default_factory=list)
    piecewise_breaks: np.ndarray = field(default_factory=lambda: np.array([]))

    @property
    def equation(self) -> str:
        if self.piecewise:
            return _piecewise_to_str(self.piecewise_polys, self.piecewise_breaks)
        return _poly_to_str(self.polynomial)


class LagrangeInterpolator:
    """拉格朗日多项式插值器

    Parameters
    ----------
    max_degree : Optional[int]
        限制插值多项式最高次数（None 表示使用全部样本）。
        建议不超过 8，更高阶会严重震荡。
    use_piecewise : bool
        是否启用分段插值，可避免 Runge 震荡，默认 True。
        分段模式使用重心插值保证数值稳定。
    window_size : int
        分段插值时每个局部窗口包含的数据点数量。
        默认 5（4 次多项式），建议不超过 7。
    boundary_mode : str
        边界外处理方式：
        - ``"clamp"``: 返回边界值（默认，防止外推失真）
        - ``"extrapolate"``: 用最近段多项式外推
        - ``"linear"``: 线性外推
    """

    def __init__(
        self,
        max_degree: Optional[int] = None,
        use_piecewise: bool = True,
        window_size: int = 5,
        boundary_mode: str = "clamp",
    ) -> None:
        if window_size < 3:
            raise ValueError("window_size 至少为 3")
        if window_size > 7:
            window_size = 7
        if boundary_mode not in ("clamp", "extrapolate", "linear"):
            raise ValueError("boundary_mode 必须为 clamp / extrapolate / linear")
        self.max_degree = max_degree
        self.use_piecewise = use_piecewise
        self.window_size = window_size
        self.boundary_mode = boundary_mode
        self.result_: Optional[LagrangeResult] = None
        self._data: Optional[Dataset] = None
        self._interp: Optional[BarycentricInterpolator] = None
        self._global_coeffs: Optional[np.ndarray] = None

    # ---------- 主入口 ----------
    def fit(self, data: Dataset) -> "LagrangeInterpolator":
        if data.size < 2:
            raise ValueError("拉格朗日插值至少需要 2 个样本点")
        data = data.sort()
        self._data = data

        if self.use_piecewise and data.size > self.window_size:
            self._fit_piecewise(data)
        else:
            self._fit_global(data)
        return self

    def predict(self, x: np.ndarray) -> np.ndarray:
        x = np.asarray(x, dtype=float)
        if self._interp is not None:
            return self._predict_piecewise(x)
        if self._global_coeffs is not None:
            return self._predict_global(x)
        raise RuntimeError("请先调用 fit 方法")

    def interpolate(self, data: Dataset, x_new: np.ndarray) -> LagrangeResult:
        self.fit(data)
        y_new = self.predict(x_new)

        if self._interp is not None:
            # 为报表生成前 3 段的多项式（仅展示用）
            demo_polys = self._extract_demo_polys(data)
            self.result_ = LagrangeResult(
                polynomial=np.array([]),
                interpolated_x=np.asarray(x_new, dtype=float),
                interpolated_y=y_new,
                degree=self.window_size - 1,
                piecewise=True,
                piecewise_polys=demo_polys,
                piecewise_breaks=data.x,
            )
        else:
            self.result_ = LagrangeResult(
                polynomial=self._global_coeffs if self._global_coeffs is not None else np.array([]),
                interpolated_x=np.asarray(x_new, dtype=float),
                interpolated_y=y_new,
                degree=(self._global_coeffs.size - 1) if self._global_coeffs is not None else 0,
                piecewise=False,
            )
        return self.result_

    # ---------- 全局拉格朗日 ----------
    def _fit_global(self, data: Dataset) -> None:
        degree = min(data.size - 1, 8)
        if self.max_degree is not None:
            degree = min(degree, self.max_degree)
        poly = lagrange(data.x, data.y)
        coeffs = np.asarray(poly.coef, dtype=float)
        if len(coeffs) - 1 > degree:
            coeffs = coeffs[: degree + 1]
        # 数值稳定检查：系数过大则截断
        if np.max(np.abs(coeffs)) > _COEFF_MAX:
            while len(coeffs) > 2 and np.max(np.abs(coeffs)) > _COEFF_MAX:
                coeffs = coeffs[:-1]
        self._global_coeffs = coeffs

    def _predict_global(self, x: np.ndarray) -> np.ndarray:
        y = np.polyval(self._global_coeffs, x)
        # 边界保护
        if self._data is not None:
            x_min, x_max = self._data.bounds
            y_left = float(np.polyval(self._global_coeffs, x_min))
            y_right = float(np.polyval(self._global_coeffs, x_max))
            if self.boundary_mode == "clamp":
                y[x < x_min] = y_left
                y[x > x_max] = y_right
            elif self.boundary_mode == "linear":
                mask_l = x < x_min
                mask_r = x > x_max
                if np.any(mask_l):
                    y[mask_l] = y_left
                if np.any(mask_r):
                    y[mask_r] = y_right
        return y

    # ---------- 分段拉格朗日（使用重心插值）----------
    def _fit_piecewise(self, data: Dataset) -> None:
        """用重心插值拟合，数值稳定，不直接生成多项式系数"""
        ws = min(self.window_size, data.size)
        self._interp = BarycentricInterpolator(data.x, data.y)

    def _extract_demo_polys(self, data: Dataset) -> List[np.ndarray]:
        """提取前几段的多项式系数用于报表（仅展示）"""
        ws = min(self.window_size, data.size)
        half = ws // 2
        polys: List[np.ndarray] = []
        n_show = min(3, data.size - 1)
        for i in range(1, n_show + 1):
            lo = max(0, i - half)
            hi = min(data.size, lo + ws)
            lo = max(0, hi - ws)
            try:
                poly = lagrange(data.x[lo:hi], data.y[lo:hi])
                coeffs = np.asarray(poly.coef, dtype=float)
                if self.max_degree is not None and len(coeffs) - 1 > self.max_degree:
                    coeffs = coeffs[: self.max_degree + 1]
                if np.max(np.abs(coeffs)) > _COEFF_MAX:
                    while len(coeffs) > 2 and np.max(np.abs(coeffs)) > _COEFF_MAX:
                        coeffs = coeffs[:-1]
                polys.append(coeffs)
            except Exception:
                polys.append(np.array([0.0]))
        return polys

    def _predict_piecewise(self, x: np.ndarray) -> np.ndarray:
        """使用重心插值预测，稳定且不会出现 Runge 震荡"""
        y = np.asarray(self._interp(x), dtype=float)

        if self._data is not None:
            x_min, x_max = self._data.bounds
            if self.boundary_mode == "clamp":
                y_left = float(self._interp(x_min))
                y_right = float(self._interp(x_max))
                y[x < x_min] = y_left
                y[x > x_max] = y_right
            elif self.boundary_mode == "linear":
                h = (x_max - x_min) * 0.01
                y_left = float(self._interp(x_min))
                y_right = float(self._interp(x_max))
                slope_l = (float(self._interp(x_min + h)) - y_left) / h
                slope_r = (y_right - float(self._interp(x_max - h))) / h
                mask_l = x < x_min
                mask_r = x > x_max
                if np.any(mask_l):
                    y[mask_l] = y_left + slope_l * (x[mask_l] - x_min)
                if np.any(mask_r):
                    y[mask_r] = y_right + slope_r * (x[mask_r] - x_max)
        return y


# ---------- 辅助 ----------
def _poly_to_str(coeffs: np.ndarray) -> str:
    coeffs = np.asarray(coeffs, dtype=float)
    if coeffs.size == 0:
        return "(重心插值，无显式多项式)"
    terms: list[str] = []
    n = len(coeffs)
    for i, c in enumerate(coeffs):
        power = n - 1 - i
        if np.isclose(c, 0.0):
            continue
        sign = "-" if c < 0 else "+"
        ac = abs(c)
        if power == 0:
            term = f"{ac:.4f}"
        elif power == 1:
            term = f"{ac:.4f}*x"
        else:
            term = f"{ac:.4f}*x^{power}"
        if not terms:
            terms.append(("-" + term) if c < 0 else term)
        else:
            terms.append(f" {sign} {term}")
    return "".join(terms) if terms else "0"


def _piecewise_to_str(
    polys: List[np.ndarray], breaks: np.ndarray
) -> str:
    if not polys:
        return "(重心分段插值，无显式全局多项式)"
    lines = []
    for i, coeffs in enumerate(polys):
        if i < len(breaks) - 1:
            x_lo = breaks[i]
            x_hi = breaks[i + 1]
            lines.append(f"  [{x_lo:.4f}, {x_hi:.4f}]: {_poly_to_str(coeffs)}")
    if len(polys) < len(breaks) - 1:
        lines.append(f"  ... (共 {len(breaks) - 1} 段，重心插值)")
    return "\n" + "\n".join(lines) if lines else "(重心分段插值)"
