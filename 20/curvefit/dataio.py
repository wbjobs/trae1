"""数据加载与基础工具模块

提供 Dataset 容器、数据加载、自适应网格生成、数据降采样等工具。
"""

from __future__ import annotations

from dataclasses import dataclass, field
from typing import Callable, List, Optional, Sequence, Tuple

import numpy as np
import pandas as pd


@dataclass
class Dataset:
    """离散数据集容器

    Attributes:
        x: 自变量数组
        y: 因变量数组
        name: 数据集名称，用于报表
    """

    x: np.ndarray
    y: np.ndarray
    name: str = "dataset"

    def __post_init__(self) -> None:
        self.x = np.asarray(self.x, dtype=float)
        self.y = np.asarray(self.y, dtype=float)
        if self.x.shape != self.y.shape:
            raise ValueError("x 和 y 长度必须相同")
        if self.x.ndim != 1:
            raise ValueError("x 和 y 必须是一维数组")

    @property
    def size(self) -> int:
        return int(self.x.size)

    @property
    def bounds(self) -> Tuple[float, float]:
        return float(self.x.min()), float(self.x.max())

    def sort(self) -> "Dataset":
        order = np.argsort(self.x)
        return Dataset(self.x[order], self.y[order], self.name)

    def __len__(self) -> int:
        return self.size


# ---------- 数据加载 ----------
def load_csv(
    path: str,
    x_col: str = "x",
    y_col: str = "y",
    name: Optional[str] = None,
) -> Dataset:
    """从 CSV 文件读取离散数据

    CSV 文件需至少包含 x_col 与 y_col 两列，允许使用 NaN 表示缺失值。
    """

    df = pd.read_csv(path)
    missing = [c for c in (x_col, y_col) if c not in df.columns]
    if missing:
        raise KeyError(f"CSV 文件缺少列: {missing}")
    return Dataset(
        x=df[x_col].to_numpy(dtype=float),
        y=df[y_col].to_numpy(dtype=float),
        name=name or path,
    )


# ---------- 演示数据 ----------
def generate_demo(
    n: int = 20,
    noise: float = 0.3,
    missing_rate: float = 0.1,
    seed: int = 42,
    name: str = "demo",
) -> Dataset:
    """生成带有噪声与缺失值的演示数据"""

    rng = np.random.default_rng(seed)
    x = np.linspace(-3.0, 3.0, n)
    y_true = np.sin(x) + 0.2 * x**2
    y = y_true + rng.normal(0.0, noise, size=n)
    if missing_rate > 0:
        n_missing = max(1, int(n * missing_rate))
        idx = rng.choice(n, size=n_missing, replace=False)
        y[idx] = np.nan
    return Dataset(x=x, y=y, name=name)


# ---------- 网格生成 ----------
def generate_grid(
    data: Dataset,
    density: int = 200,
) -> np.ndarray:
    """在数据 x 范围内生成等间距插值点"""
    x_min, x_max = data.bounds
    return np.linspace(x_min, x_max, density)


def generate_adaptive_grid(
    data: Dataset,
    base_density: int = 100,
    refine_density: int = 300,
    curvature_threshold: float = 0.1,
) -> np.ndarray:
    """自适应网格：在曲率大（二阶导数变化大）的区域加密采样点

    利用数据 y 的二阶差分估计局部曲率，在高曲率区域叠加额外采样点。
    """
    data = data.sort()
    x = data.x
    y = data.y
    n = x.size
    if n < 3:
        return np.linspace(x.min(), x.max(), base_density)

    base_grid = np.linspace(x.min(), x.max(), base_density)
    # 估计二阶差分
    d2 = np.abs(np.diff(y, n=2))
    max_d2 = d2.max() if d2.size > 0 else 1.0
    if max_d2 == 0:
        max_d2 = 1.0
    norm_d2 = d2 / max_d2

    # 在每个高曲率区间添加额外点
    extra_points: List[float] = []
    for i in range(n - 2):
        if norm_d2[i] > curvature_threshold:
            lo, hi = x[i], x[i + 2]
            n_extra = int(refine_density * norm_d2[i] / base_density)
            if n_extra > 0:
                extra_points.extend(
                    np.linspace(lo, hi, n_extra + 2)[1:-1].tolist()
                )

    all_points = np.concatenate([base_grid, np.array(extra_points)])
    all_points = np.unique(np.sort(all_points))
    return all_points


# ---------- 工具函数 ----------
def mask_missing(y: np.ndarray) -> np.ndarray:
    """返回非缺失值的布尔掩码"""
    return ~np.isnan(np.asarray(y, dtype=float))


def downsample(data: Dataset, max_points: int = 500) -> Dataset:
    """将高密度数据降采样到最多 max_points 个点

    使用等间距索引采样，保持原始数据的分布特征。
    """
    if data.size <= max_points:
        return data
    data = data.sort()
    step = data.size / max_points
    indices = np.unique(np.floor(np.arange(0, data.size, step)).astype(int))
    indices = np.append(indices, data.size - 1)
    indices = np.unique(indices)
    return Dataset(x=data.x[indices], y=data.y[indices], name=data.name)


# ---------- 批处理工具 ----------
@dataclass
class BatchResult:
    """批处理单个数据集的结果"""
    name: str
    n_samples: int
    method: str
    equation: str
    metrics: dict


class BatchProcessor:
    """批量数据处理器

    对多个数据集依次执行预处理与拟合/插值，将结果汇总输出。
    内部通过降采样与限制网格密度避免大批量数据卡顿。

    Parameters
    ----------
    max_points_per_dataset : int
        单个数据集最多保留的点数（降采样上限）
    """

    def __init__(self, max_points_per_dataset: int = 500) -> None:
        self.max_points_per_dataset = max_points_per_dataset
        self.results: List[BatchResult] = []

    def process(
        self,
        datasets: Sequence[Dataset],
        preprocessor_fn: Callable[[Dataset], Dataset],
        fit_fn: Callable[[Dataset], BatchResult],
        verbose: bool = True,
    ) -> List[BatchResult]:
        """批量处理

        Parameters
        ----------
        datasets : Sequence[Dataset]
            待处理的数据集序列
        preprocessor_fn : callable
            预处理函数，输入 Dataset 返回 Dataset
        fit_fn : callable
            拟合函数，输入 Dataset 返回 BatchResult
        verbose : bool
            是否打印进度
        """
        self.results = []
        for i, ds in enumerate(datasets):
            if verbose:
                print(f"[批处理 {i + 1}/{len(datasets)}] 处理 {ds.name} ({ds.size} 点)")
            ds_ds = downsample(ds, self.max_points_per_dataset)
            processed = preprocessor_fn(ds_ds)
            result = fit_fn(processed)
            self.results.append(result)
        return self.results

    def summary(self) -> str:
        lines = ["=" * 60, "批处理汇总", "=" * 60]
        for r in self.results:
            lines.append(
                f"[{r.method}] {r.name}: 样本={r.n_samples}, "
                f"方程={r.equation}"
            )
            for k, v in r.metrics.items():
                lines.append(f"  {k}: {v}")
        return "\n".join(lines)
