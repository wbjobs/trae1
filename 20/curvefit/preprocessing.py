"""数据预处理模块

提供缺失值检测与插值、平滑处理、异常值剔除等预处理能力。

异常值检测方法：
- ``IQR``: 基于四分位距的全局统计检测
- ``ZSCORE``: 基于 Z-Score 的全局统计检测
- ``SMART``: 智能检测，结合局部残差分析 + 孤立点检测
- ``RESIDUAL``: 基于拟合残差的检测（对局部趋势偏离敏感）
- ``NONE``: 不检测
"""

from __future__ import annotations

from dataclasses import dataclass, field
from enum import Enum
from typing import List, Optional

import numpy as np
from scipy.signal import savgol_filter

from .dataio import Dataset, mask_missing


class SmoothingMethod(str, Enum):
    MOVING_AVERAGE = "moving_average"
    SAVITZKY_GOLAY = "savgol"
    NONE = "none"


class OutlierMethod(str, Enum):
    IQR = "iqr"
    ZSCORE = "zscore"
    SMART = "smart"
    RESIDUAL = "residual"
    NONE = "none"


class MissingMethod(str, Enum):
    LINEAR = "linear"
    NEAREST = "nearest"
    CUBIC = "cubic"
    DROP = "drop"


@dataclass
class OutlierDetail:
    """单个异常点的详细信息"""
    index: int
    x: float
    y: float
    reason: str
    score: float


@dataclass
class PreprocessReport:
    """预处理执行报告"""

    original_size: int
    final_size: int
    missing_count: int = 0
    outlier_count: int = 0
    filled_method: str = ""
    smoothing_method: str = ""
    outlier_method: str = ""
    removed_indices: List[int] = field(default_factory=list)
    outlier_details: List[OutlierDetail] = field(default_factory=list)

    def summary(self) -> str:
        return (
            f"[预处理报告] 样本: {self.original_size} -> {self.final_size}, "
            f"缺失值: {self.missing_count} ({self.filled_method}), "
            f"平滑: {self.smoothing_method}, "
            f"异常值: {self.outlier_count} ({self.outlier_method})"
        )


class DataPreprocessor:
    """离散数据预处理器

    Parameters
    ----------
    missing_method : MissingMethod
        缺失值处理方式
    smoothing : SmoothingMethod
        平滑方法
    window : int
        平滑窗口，必须为奇数
    polyorder : int
        Savitzky-Golay 多项式阶数
    outlier_method : OutlierMethod
        异常值剔除方法
    outlier_threshold : float
        异常值判定阈值（含义取决于方法）
    outlier_k : int
        SMART/RESIDUAL 方法中用于局部拟合的邻域点数
    """

    def __init__(
        self,
        missing_method: MissingMethod = MissingMethod.LINEAR,
        smoothing: SmoothingMethod = SmoothingMethod.SAVITZKY_GOLAY,
        window: int = 5,
        polyorder: int = 2,
        outlier_method: OutlierMethod = OutlierMethod.SMART,
        outlier_threshold: float = 2.5,
        outlier_k: int = 5,
    ) -> None:
        if window % 2 == 0:
            raise ValueError("平滑窗口必须为奇数")
        if outlier_k < 3:
            raise ValueError("outlier_k 至少为 3")
        self.missing_method = missing_method
        self.smoothing = smoothing
        self.window = window
        self.polyorder = polyorder
        self.outlier_method = outlier_method
        self.outlier_threshold = outlier_threshold
        self.outlier_k = outlier_k
        self.report: Optional[PreprocessReport] = None

    # ---------- 主入口 ----------
    def fit_transform(self, data: Dataset) -> Dataset:
        """执行完整预处理流程并返回清洗后的 Dataset"""

        data = data.sort()
        original_size = data.size

        x, y = data.x.copy(), data.y.copy()

        # 1. 缺失值处理
        y, missing_count = self._handle_missing(x, y)

        # 2. 异常值剔除
        x, y, outlier_count, removed, details = self._remove_outliers(x, y)

        # 3. 平滑处理
        y = self._smooth(y)

        processed = Dataset(x=x, y=y, name=data.name)
        self.report = PreprocessReport(
            original_size=original_size,
            final_size=processed.size,
            missing_count=missing_count,
            outlier_count=outlier_count,
            filled_method=self.missing_method.value,
            smoothing_method=self.smoothing.value,
            outlier_method=self.outlier_method.value,
            removed_indices=removed,
            outlier_details=details,
        )
        return processed

    # ---------- 缺失值处理 ----------
    def _handle_missing(
        self, x: np.ndarray, y: np.ndarray
    ) -> tuple[np.ndarray, int]:
        mask = mask_missing(y)
        missing_count = int(mask.size - mask.sum())

        if missing_count == 0:
            return y, 0

        if self.missing_method == MissingMethod.DROP:
            return y[mask], missing_count

        if mask.sum() < 2:
            raise ValueError("有效样本不足，无法进行插值填充")

        x_known = x[mask]
        y_known = y[mask]

        kind_map = {
            MissingMethod.LINEAR: 1,
            MissingMethod.NEAREST: 0,
            MissingMethod.CUBIC: 3,
        }
        from scipy.interpolate import interp1d

        f = interp1d(
            x_known,
            y_known,
            kind=kind_map[self.missing_method],
            fill_value="extrapolate",
            bounds_error=False,
        )
        y_filled = y.copy()
        y_filled[~mask] = f(x[~mask])
        return y_filled, missing_count

    # ---------- 异常值剔除 ----------
    def _remove_outliers(
        self, x: np.ndarray, y: np.ndarray
    ) -> tuple[np.ndarray, np.ndarray, int, List[int], List[OutlierDetail]]:
        if self.outlier_method == OutlierMethod.NONE:
            return x, y, 0, [], []

        details: List[OutlierDetail] = []

        if self.outlier_method == OutlierMethod.IQR:
            keep, reasons, scores = self._detect_iqr(y)
        elif self.outlier_method == OutlierMethod.ZSCORE:
            keep, reasons, scores = self._detect_zscore(y)
        elif self.outlier_method == OutlierMethod.RESIDUAL:
            keep, reasons, scores = self._detect_residual(x, y)
        elif self.outlier_method == OutlierMethod.SMART:
            keep, reasons, scores = self._detect_smart(x, y)
        else:
            return x, y, 0, [], []

        removed = np.where(~keep)[0].tolist()
        for idx in removed:
            details.append(
                OutlierDetail(
                    index=int(idx),
                    x=float(x[idx]),
                    y=float(y[idx]),
                    reason=reasons[idx] if idx < len(reasons) else "unknown",
                    score=float(scores[idx]) if idx < len(scores) else 0.0,
                )
            )
        return x[keep], y[keep], len(removed), removed, details

    def _detect_iqr(self, y: np.ndarray) -> tuple[np.ndarray, List[str], np.ndarray]:
        q1, q3 = np.percentile(y, [25, 75])
        iqr = q3 - q1
        lower = q1 - self.outlier_threshold * iqr
        upper = q3 + self.outlier_threshold * iqr
        keep = (y >= lower) & (y <= upper)
        scores = np.maximum(np.abs(y - q3) / max(iqr, 1e-12), np.abs(y - q1) / max(iqr, 1e-12))
        reasons = ["iqr" if not k else "" for k in keep]
        return keep, reasons, scores

    def _detect_zscore(self, y: np.ndarray) -> tuple[np.ndarray, List[str], np.ndarray]:
        mu = np.mean(y)
        sigma = np.std(y)
        if sigma == 0:
            return np.ones_like(y, dtype=bool), [], np.zeros_like(y)
        z = np.abs((y - mu) / sigma)
        keep = z <= self.outlier_threshold
        reasons = ["zscore" if not k else "" for k in keep]
        return keep, reasons, z

    def _detect_residual(
        self, x: np.ndarray, y: np.ndarray
    ) -> tuple[np.ndarray, List[str], np.ndarray]:
        """基于局部线性拟合残差检测异常点

        对每个点，使用其 k 个邻居拟合低次多项式，计算残差的 Z-Score。
        """
        n = y.size
        residuals = np.zeros(n)
        k = min(self.outlier_k, n)
        half = k // 2

        for i in range(n):
            lo = max(0, i - half)
            hi = min(n, lo + k)
            lo = max(0, hi - k)
            x_seg = x[lo:hi]
            y_seg = y[lo:hi]
            local_i = i - lo
            # 拟合低次多项式并计算该点的残差
            deg = min(2, len(x_seg) - 1)
            coeffs = np.polyfit(x_seg, y_seg, deg)
            y_pred = np.polyval(coeffs, x_seg)
            residuals[i] = np.abs(y_seg[local_i] - y_pred[local_i])

        sigma = np.std(residuals)
        if sigma == 0:
            return np.ones(n, dtype=bool), [], residuals
        z = residuals / sigma
        keep = z <= self.outlier_threshold
        reasons = ["residual" if not k else "" for k in keep]
        return keep, reasons, z

    def _detect_smart(
        self, x: np.ndarray, y: np.ndarray
    ) -> tuple[np.ndarray, List[str], np.ndarray]:
        """智能异常点检测：综合残差 + 全局 Z-Score + 孤立度

        一个点被判定为异常需同时满足：
        1. 局部残差 Z-Score 超过阈值（趋势偏离）
        2. 或者全局 Z-Score 超过阈值（全局偏离）
        3. 且与最近邻居的距离较大（孤立点）
        """
        n = y.size

        # 全局 Z-Score
        mu = np.mean(y)
        sigma_y = np.std(y)
        if sigma_y == 0:
            return np.ones(n, dtype=bool), [], np.zeros(n)
        z_global = np.abs((y - mu) / sigma_y)

        # 局部残差
        k = min(self.outlier_k, n)
        half = k // 2
        residuals = np.zeros(n)
        for i in range(n):
            lo = max(0, i - half)
            hi = min(n, lo + k)
            lo = max(0, hi - k)
            x_seg = x[lo:hi]
            y_seg = y[lo:hi]
            local_i = i - lo
            deg = min(2, len(x_seg) - 1)
            coeffs = np.polyfit(x_seg, y_seg, deg)
            y_pred = np.polyval(coeffs, x_seg)
            residuals[i] = np.abs(y_seg[local_i] - y_pred[local_i])

        sigma_r = np.std(residuals)
        z_residual = residuals / max(sigma_r, 1e-12)

        # 孤立度：与最近邻居的 y 距离
        y_diff = np.abs(np.diff(y))
        isolation = np.zeros(n)
        for i in range(n):
            neighbors = []
            if i > 0:
                neighbors.append(y_diff[i - 1])
            if i < n - 1:
                neighbors.append(y_diff[i])
            isolation[i] = np.max(neighbors) if neighbors else 0.0
        sigma_iso = np.std(isolation)
        z_isolation = isolation / max(sigma_iso, 1e-12)

        # 综合判定：残差异常 AND (全局异常 OR 孤立异常)
        keep = np.ones(n, dtype=bool)
        reasons = [""] * n
        scores = np.zeros(n)

        for i in range(n):
            is_residual = z_residual[i] > self.outlier_threshold
            is_global = z_global[i] > self.outlier_threshold * 1.5
            is_isolated = z_isolation[i] > self.outlier_threshold
            combined_score = max(z_residual[i], z_global[i] * 0.6, z_isolation[i] * 0.4)
            scores[i] = combined_score

            if is_residual and (is_global or is_isolated):
                keep[i] = False
                r = []
                if is_residual:
                    r.append("residual")
                if is_global:
                    r.append("global")
                if is_isolated:
                    r.append("isolated")
                reasons[i] = "+".join(r)

        return keep, reasons, scores

    # ---------- 平滑处理 ----------
    def _smooth(self, y: np.ndarray) -> np.ndarray:
        if self.smoothing == SmoothingMethod.NONE:
            return y
        if y.size < self.window:
            return y
        if self.smoothing == SmoothingMethod.MOVING_AVERAGE:
            kernel = np.ones(self.window) / self.window
            return np.convolve(y, kernel, mode="same")
        if self.smoothing == SmoothingMethod.SAVITZKY_GOLAY:
            return savgol_filter(
                y, window_length=self.window, polyorder=self.polyorder
            )
        return y
