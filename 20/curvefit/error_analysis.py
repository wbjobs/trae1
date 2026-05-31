"""误差分析与报表生成模块"""

from __future__ import annotations

import json
from dataclasses import dataclass, asdict, field
from datetime import datetime
from typing import Any, Dict, List, Optional

import numpy as np


@dataclass
class ErrorMetrics:
    """标准误差指标"""

    mae: float
    mse: float
    rmse: float
    r2: float
    max_abs_error: float
    mean_abs_percent_error: float
    bias: float

    def to_dict(self) -> Dict[str, float]:
        return {k: round(float(v), 6) for k, v in asdict(self).items()}


@dataclass
class FittingReport:
    """完整的拟合/插值报表"""

    name: str
    method: str
    degree: Optional[int] = None
    equation: str = ""
    n_samples: int = 0
    metrics: Optional[ErrorMetrics] = None
    extra: Dict[str, Any] = field(default_factory=dict)
    generated_at: str = field(default_factory=lambda: datetime.now().isoformat(timespec="seconds"))

    def to_dict(self) -> Dict[str, Any]:
        d = {
            "name": self.name,
            "method": self.method,
            "degree": self.degree,
            "equation": self.equation,
            "n_samples": self.n_samples,
            "metrics": self.metrics.to_dict() if self.metrics else None,
            "extra": self.extra,
            "generated_at": self.generated_at,
        }
        return d

    def summary(self) -> str:
        lines = [
            f"[{self.method}] {self.name}",
            f"  样本数: {self.n_samples}",
        ]
        if self.degree is not None:
            lines.append(f"  阶数: {self.degree}")
        if self.equation:
            lines.append(f"  方程: {self.equation}")
        if self.metrics:
            m = self.metrics
            lines.append(
                f"  MAE={m.mae:.6f}  MSE={m.mse:.6f}  RMSE={m.rmse:.6f}  "
                f"R²={m.r2:.6f}  MaxAbs={m.max_abs_error:.6f}  "
                f"MAPE={m.mean_abs_percent_error:.4f}%  Bias={m.bias:.6f}"
            )
        if self.extra:
            lines.append(f"  附加信息: {self.extra}")
        return "\n".join(lines)


class ErrorAnalyzer:
    """误差分析器

    计算常用回归误差指标，并支持将多条报表汇总导出为文本或 JSON。
    """

    def __init__(self) -> None:
        self.reports: List[FittingReport] = []

    # ---------- 指标计算 ----------
    def compute_metrics(
        self, y_true: np.ndarray, y_pred: np.ndarray
    ) -> ErrorMetrics:
        y_true = np.asarray(y_true, dtype=float)
        y_pred = np.asarray(y_pred, dtype=float)
        if y_true.shape != y_pred.shape:
            raise ValueError("y_true 与 y_pred 形状不一致")

        err = y_pred - y_true
        abs_err = np.abs(err)
        mae = float(np.mean(abs_err))
        mse = float(np.mean(err**2))
        rmse = float(np.sqrt(mse))
        max_abs = float(np.max(abs_err))

        nonzero = np.abs(y_true) > 1e-12
        if np.any(nonzero):
            mape = float(np.mean(abs_err[nonzero] / np.abs(y_true[nonzero])) * 100.0)
        else:
            mape = float("inf")

        r2 = self._r2(y_true, y_pred)
        bias = float(np.mean(err))
        return ErrorMetrics(
            mae=mae,
            mse=mse,
            rmse=rmse,
            r2=r2,
            max_abs_error=max_abs,
            mean_abs_percent_error=mape,
            bias=bias,
        )

    @staticmethod
    def _r2(y_true: np.ndarray, y_pred: np.ndarray) -> float:
        ss_res = float(np.sum((y_true - y_pred) ** 2))
        ss_tot = float(np.sum((y_true - np.mean(y_true)) ** 2))
        if ss_tot == 0:
            return 1.0 if ss_res == 0 else 0.0
        return 1.0 - ss_res / ss_tot

    # ---------- 报表管理 ----------
    def add_report(self, report: FittingReport) -> None:
        self.reports.append(report)

    def build_report(
        self,
        name: str,
        method: str,
        y_true: np.ndarray,
        y_pred: np.ndarray,
        equation: str = "",
        degree: Optional[int] = None,
        n_samples: Optional[int] = None,
        extra: Optional[Dict[str, Any]] = None,
    ) -> FittingReport:
        metrics = self.compute_metrics(y_true, y_pred)
        report = FittingReport(
            name=name,
            method=method,
            degree=degree,
            equation=equation,
            n_samples=n_samples or len(y_true),
            metrics=metrics,
            extra=extra or {},
        )
        self.reports.append(report)
        return report

    # ---------- 导出 ----------
    def text_report(self) -> str:
        sep = "=" * 72
        header = f"{sep}\n离散数据拟合/插值 误差分析报表\n{sep}"
        body = "\n\n".join(r.summary() for r in self.reports)
        return f"{header}\n\n{body}\n{sep}\n"

    def save_text(self, path: str) -> None:
        with open(path, "w", encoding="utf-8") as f:
            f.write(self.text_report())

    def save_json(self, path: str) -> None:
        data = {
            "generated_at": datetime.now().isoformat(timespec="seconds"),
            "reports": [r.to_dict() for r in self.reports],
        }
        with open(path, "w", encoding="utf-8") as f:
            json.dump(data, f, ensure_ascii=False, indent=2)
