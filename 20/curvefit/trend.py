"""拟合曲线趋势预测模块

基于已拟合的模型对未来趋势进行外推预测，并提供基于残差 Bootstrap 的置信区间。

支持：
- 多项式趋势预测（线性外推 / 多项式外推）
- 基于残差 Bootstrap 的置信区间
- 多步外推预测
"""

from __future__ import annotations

from dataclasses import dataclass, field
from enum import Enum
from typing import Callable, List, Optional, Tuple

import numpy as np

from .dataio import Dataset


class TrendModel(str, Enum):
    LINEAR = "linear"
    POLYNOMIAL = "polynomial"
    EXPONENTIAL = "exponential"


@dataclass
class TrendPrediction:
    """趋势预测结果"""
    x_forecast: np.ndarray
    y_forecast: np.ndarray
    y_lower: np.ndarray
    y_upper: np.ndarray
    confidence: float
    model_type: str
    direction: str
    slope: float
    fitted_coeffs: Optional[np.ndarray] = None

    @property
    def interval_width(self) -> np.ndarray:
        return self.y_upper - self.y_lower

    @property
    def is_increasing(self) -> bool:
        return self.slope > 0

    @property
    def is_decreasing(self) -> bool:
        return self.slope < 0


class TrendPredictor:
    """趋势预测器

    对已拟合的模型进行外推预测，并计算置信区间。

    Parameters
    ----------
    model_type : TrendModel
        趋势模型类型：
        - ``linear``: 线性趋势（一阶多项式）
        - ``polynomial``: 高阶多项式趋势
        - ``exponential``: 指数趋势
    confidence : float
        置信水平（0-1），默认 0.95
    n_bootstrap : int
        Bootstrap 抽样次数，用于估计置信区间
    poly_degree : int
        polynomial 模型的阶数
    """

    def __init__(
        self,
        model_type: TrendModel = TrendModel.LINEAR,
        confidence: float = 0.95,
        n_bootstrap: int = 1000,
        poly_degree: int = 2,
    ) -> None:
        if not 0 < confidence < 1:
            raise ValueError("confidence 必须在 (0, 1) 之间")
        self.model_type = model_type
        self.confidence = confidence
        self.n_bootstrap = n_bootstrap
        self.poly_degree = poly_degree
        self._coeffs: Optional[np.ndarray] = None
        self._fitted_data: Optional[Dataset] = None
        self._residuals: Optional[np.ndarray] = None
        self._last_prediction: Optional[TrendPrediction] = None

    @property
    def last_prediction(self) -> Optional[TrendPrediction]:
        return self._last_prediction

    # ---------- 拟合趋势 ----------
    def fit(self, data: Dataset) -> "TrendPredictor":
        """拟合趋势模型

        Parameters
        ----------
        data : Dataset
            用于拟合趋势的数据（通常是拟合后的曲线值）
        """
        data = data.sort()
        self._fitted_data = data

        if self.model_type == TrendModel.LINEAR:
            self._coeffs = np.polyfit(data.x, data.y, 1)
        elif self.model_type == TrendModel.POLYNOMIAL:
            deg = min(self.poly_degree, data.size - 1)
            self._coeffs = np.polyfit(data.x, data.y, deg)
        elif self.model_type == TrendModel.EXPONENTIAL:
            # y = a * exp(b * x)  =>  log(y) = log(a) + b*x
            y_pos = data.y
            if np.any(y_pos <= 0):
                y_shifted = y_pos - y_pos.min() + 1.0
            else:
                y_shifted = y_pos
            log_y = np.log(y_shifted)
            coeffs = np.polyfit(data.x, log_y, 1)
            self._coeffs = np.array([
                coeffs[0],
                np.exp(coeffs[1]),
            ])  # [b, a]
        else:
            raise ValueError(f"未知模型类型: {self.model_type}")

        # 计算残差（在原始 x 上）
        y_pred = self._predict(data.x)
        self._residuals = data.y - y_pred
        return self

    # ---------- 预测 ----------
    def predict(
        self,
        x_forecast: np.ndarray,
        use_bootstrap: bool = True,
    ) -> TrendPrediction:
        """预测未来趋势

        Parameters
        ----------
        x_forecast : np.ndarray
            需要预测的 x 点
        use_bootstrap : bool
            是否使用 Bootstrap 估计置信区间

        Returns
        -------
        TrendPrediction
        """
        if self._coeffs is None:
            raise RuntimeError("请先调用 fit 方法")
        x_forecast = np.asarray(x_forecast, dtype=float)
        y_forecast = self._predict(x_forecast)

        if use_bootstrap and self._residuals is not None and self._fitted_data is not None:
            y_lower, y_upper = self._bootstrap_confidence(x_forecast)
        else:
            sigma = float(np.std(self._residuals)) if self._residuals is not None else 0.0
            z = _zscore_for_confidence(self.confidence)
            margin = z * sigma
            y_lower = y_forecast - margin
            y_upper = y_forecast + margin

        # 计算斜率（在预测区间的起点和终点）
        if len(x_forecast) >= 2:
            slope = float(
                (y_forecast[-1] - y_forecast[0]) / (x_forecast[-1] - x_forecast[0])
            )
        else:
            slope = 0.0
        direction = "increasing" if slope > 0 else ("decreasing" if slope < 0 else "stable")

        result = TrendPrediction(
            x_forecast=x_forecast,
            y_forecast=y_forecast,
            y_lower=y_lower,
            y_upper=y_upper,
            confidence=self.confidence,
            model_type=self.model_type.value,
            direction=direction,
            slope=slope,
            fitted_coeffs=self._coeffs.copy(),
        )
        self._last_prediction = result
        return result

    def forecast(
        self,
        steps: int = 10,
        step_size: Optional[float] = None,
        use_bootstrap: bool = True,
    ) -> TrendPrediction:
        """便捷方法：基于最后一个数据点外推 steps 步

        Parameters
        ----------
        steps : int
            外推步数
        step_size : Optional[float]
            步长。为 None 时自动根据数据间距估计。
        use_bootstrap : bool
            是否使用 Bootstrap 估计置信区间
        """
        if self._fitted_data is None:
            raise RuntimeError("请先调用 fit 方法")
        x = self._fitted_data.x
        if step_size is None:
            step_size = float(np.mean(np.diff(x))) if len(x) > 1 else 1.0
        x_last = float(x[-1])
        x_forecast = x_last + np.arange(1, steps + 1) * step_size
        return self.predict(x_forecast, use_bootstrap=use_bootstrap)

    # ---------- 内部 ----------
    def _predict(self, x: np.ndarray) -> np.ndarray:
        x = np.asarray(x, dtype=float)
        if self.model_type == TrendModel.EXPONENTIAL:
            b, a = self._coeffs[0], self._coeffs[1]
            return a * np.exp(b * x)
        return np.polyval(self._coeffs, x)

    def _bootstrap_confidence(
        self, x_forecast: np.ndarray
    ) -> Tuple[np.ndarray, np.ndarray]:
        """基于残差 Bootstrap 估计预测区间"""
        residuals = self._residuals
        x_orig = self._fitted_data.x
        n = len(residuals)
        n_boot = self.n_bootstrap

        y_boot = np.empty((n_boot, len(x_forecast)))
        rng = np.random.default_rng(42)

        for i in range(n_boot):
            idx = rng.integers(0, n, size=n)
            y_sample = self._predict(x_orig) + residuals[idx]
            try:
                if self.model_type == TrendModel.LINEAR:
                    c = np.polyfit(x_orig, y_sample, 1)
                elif self.model_type == TrendModel.POLYNOMIAL:
                    deg = min(self.poly_degree, n - 1)
                    c = np.polyfit(x_orig, y_sample, deg)
                else:  # exponential
                    y_pos = y_sample
                    if np.any(y_pos <= 0):
                        y_shifted = y_pos - y_pos.min() + 1.0
                    else:
                        y_shifted = y_pos
                    log_y = np.log(y_shifted)
                    c_raw = np.polyfit(x_orig, log_y, 1)
                    c = np.array([c_raw[0], np.exp(c_raw[1])])
                    y_boot[i] = c[1] * np.exp(c[0] * x_forecast)
                    continue
                y_boot[i] = np.polyval(c, x_forecast)
            except Exception:
                y_boot[i] = self._predict(x_forecast)

        alpha = 1.0 - self.confidence
        y_lower = np.percentile(y_boot, 100 * alpha / 2, axis=0)
        y_upper = np.percentile(y_boot, 100 * (1 - alpha / 2), axis=0)
        return y_lower, y_upper


def _zscore_for_confidence(confidence: float) -> float:
    """标准正态分布的分位数近似"""
    from scipy.stats import norm

    return float(norm.ppf(1 - (1 - confidence) / 2))
