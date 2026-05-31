"""多项式拟合模块（最小二乘，支持自定义阶数、正则化与交叉验证）

提供稳健的多项式拟合，包含：
- 基于留一交叉验证 (LOOCV) 的自动阶数选择
- 条件数约束：条件数过大时自动降阶
- L2 正则化（Ridge），可自动搜索最优系数
- QR 分解求解，数值更稳定
- 边界外推保护
"""

from __future__ import annotations

from dataclasses import dataclass, field
from typing import List, Optional, Tuple

import numpy as np

from .dataio import Dataset


@dataclass
class PolynomialResult:
    """多项式拟合结果"""

    coefficients: np.ndarray
    degree: int
    fitted_x: np.ndarray
    fitted_y: np.ndarray
    r2: float
    rmse: float
    condition_number: float
    cv_rmse: float = 0.0
    ridge: float = 0.0
    used_cv: bool = False

    @property
    def equation(self) -> str:
        return _poly_to_str(self.coefficients)


class PolynomialFitter:
    """最小二乘多项式拟合器

    Parameters
    ----------
    degree : int
        拟合多项式阶数（若启用 auto_select 则作为初始参考）
    ridge : float
        L2 正则化系数（若 auto_ridge=True 则为搜索范围）
    auto_select : bool
        是否根据交叉验证自动选择最优阶数
    max_degree : int
        自动选择时搜索的最高阶数
    max_cond : float
        范德蒙德矩阵最大允许条件数，超过则自动降阶
    cv_folds : int
        交叉验证折数，-1 表示留一法 (LOOCV)
    auto_ridge : bool
        是否自动搜索最优 ridge 系数
    ridge_grid : List[float]
        ridge 搜索候选值
    """

    _COND_MAX_DEFAULT = 1e10

    def __init__(
        self,
        degree: int = 3,
        ridge: float = 0.0,
        auto_select: bool = False,
        max_degree: int = 10,
        max_cond: float = _COND_MAX_DEFAULT,
        cv_folds: int = -1,
        auto_ridge: bool = False,
        ridge_grid: Optional[List[float]] = None,
    ) -> None:
        if degree < 0:
            raise ValueError("阶数必须为非负整数")
        self.degree = degree
        self.ridge = ridge
        self.auto_select = auto_select
        self.max_degree = max_degree
        self.max_cond = max_cond
        self.cv_folds = cv_folds
        self.auto_ridge = auto_ridge
        self.ridge_grid = ridge_grid or [0.0, 1e-5, 1e-4, 1e-3, 1e-2, 1e-1, 1.0]
        self.result_: Optional[PolynomialResult] = None

    # ---------- 核心 ----------
    def fit(self, data: Dataset) -> "PolynomialFitter":
        if data.size < 2:
            raise ValueError("拟合至少需要 2 个样本点")

        n = data.size
        if self.auto_select:
            best_deg, best_ridge, best_cv = self._auto_select(data)
            self.degree = best_deg
            if self.auto_ridge:
                self.ridge = best_ridge
        elif self.auto_ridge:
            _, best_ridge, best_cv = self._auto_select(
                data, fixed_degree=self.degree
            )
            self.ridge = best_ridge
            best_cv = best_cv
        else:
            # 条件数约束检查
            while self.degree > 0:
                coeffs, cond = self._fit(data, self.degree, self.ridge)
                if cond <= self.max_cond or self.degree <= 1:
                    break
                self.degree -= 1
            best_cv = 0.0

        coeffs, cond = self._fit(data, self.degree, self.ridge)
        fitted_y = np.polyval(coeffs, data.x)
        r2 = _r2_score(data.y, fitted_y)
        rmse = float(np.sqrt(np.mean((data.y - fitted_y) ** 2)))

        self.result_ = PolynomialResult(
            coefficients=coeffs,
            degree=self.degree,
            fitted_x=data.x,
            fitted_y=fitted_y,
            r2=r2,
            rmse=rmse,
            condition_number=cond,
            cv_rmse=best_cv if best_cv else 0.0,
            ridge=self.ridge,
            used_cv=self.auto_select or self.auto_ridge,
        )
        return self

    def predict(self, x: np.ndarray, clamp: bool = True) -> np.ndarray:
        """预测

        Parameters
        ----------
        x : np.ndarray
            预测点
        clamp : bool
            是否对超出范围的点进行边界保护（默认 True）
        """
        if self.result_ is None:
            raise RuntimeError("请先调用 fit 方法")
        x = np.asarray(x, dtype=float)
        y = np.polyval(self.result_.coefficients, x)
        if clamp and self.result_.fitted_x.size > 0:
            x_min = float(self.result_.fitted_x.min())
            x_max = float(self.result_.fitted_x.max())
            y_left = float(np.polyval(self.result_.coefficients, x_min))
            y_right = float(np.polyval(self.result_.coefficients, x_max))
            y[x < x_min] = y_left
            y[x > x_max] = y_right
        return y

    # ---------- 内部 ----------
    def _fit(
        self, data: Dataset, degree: int, ridge: float
    ) -> Tuple[np.ndarray, float]:
        """使用 QR 分解求解最小二乘，返回系数与条件数"""
        x, y = data.x, data.y
        degree = min(degree, n - 1) if (n := x.size) > 1 else degree
        A = np.vander(x, N=degree + 1, increasing=False)
        cond = float(np.linalg.cond(A)) if A.shape[1] > 1 else 1.0

        if ridge > 0:
            I = np.eye(A.shape[1])
            A_aug = np.vstack([A, np.sqrt(ridge) * I])
            y_aug = np.concatenate([y, np.zeros(A.shape[1])])
            Q, R = np.linalg.qr(A_aug, mode="reduced")
            coeffs = np.linalg.solve(R, Q.T @ y_aug)
        else:
            Q, R = np.linalg.qr(A, mode="reduced")
            coeffs = np.linalg.solve(R, Q.T @ y)
        return coeffs, cond

    def _auto_select(
        self, data: Dataset, fixed_degree: Optional[int] = None
    ) -> Tuple[int, float, float]:
        """基于交叉验证自动选择阶数和/或 ridge 系数

        Returns
        -------
        best_degree, best_ridge, best_cv_rmse
        """
        n = data.size
        max_d = min(self.max_degree, n - 1, 15)

        best_deg = self.degree if fixed_degree is None else fixed_degree
        best_ridge = self.ridge
        best_cv = np.inf

        degrees = (
            [fixed_degree]
            if fixed_degree is not None
            else range(1, max_d + 1)
        )
        ridges = self.ridge_grid if self.auto_ridge else [self.ridge]

        for d in degrees:
            # 条件数硬约束
            coeffs_check, cond_check = self._fit(data, d, 0.0)
            if cond_check > self.max_cond and d > 3:
                continue
            for r in ridges:
                cv_err = self._cross_validate(data, d, r)
                if cv_err < best_cv:
                    best_cv = cv_err
                    best_deg = d
                    best_ridge = r

        return best_deg, best_ridge, float(best_cv)

    def _cross_validate(
        self, data: Dataset, degree: int, ridge: float
    ) -> float:
        """交叉验证计算 RMSE（留一法或 k 折）"""
        x, y = data.x, data.y
        n = x.size
        k = n if self.cv_folds == -1 else min(self.cv_folds, n)
        rng = np.random.default_rng(0)
        indices = np.arange(n)
        if k < n:
            rng.shuffle(indices)
            folds = np.array_split(indices, k)
        else:
            folds = [[i] for i in indices]

        errors: List[float] = []
        for fold in folds:
            train_mask = np.ones(n, dtype=bool)
            train_mask[fold] = False
            train_data = Dataset(x=x[train_mask], y=y[train_mask])
            if train_data.size < degree + 1:
                continue
            try:
                coeffs, _ = self._fit(train_data, degree, ridge)
                pred = np.polyval(coeffs, x[fold])
                errors.extend((pred - y[fold]) ** 2)
            except np.linalg.LinAlgError:
                continue

        if not errors:
            return np.inf
        return float(np.sqrt(np.mean(errors)))


# ---------- 辅助 ----------
def _r2_score(y_true: np.ndarray, y_pred: np.ndarray) -> float:
    ss_res = float(np.sum((y_true - y_pred) ** 2))
    ss_tot = float(np.sum((y_true - np.mean(y_true)) ** 2))
    if ss_tot == 0:
        return 1.0 if ss_res == 0 else 0.0
    return 1.0 - ss_res / ss_tot


def _poly_to_str(coeffs: np.ndarray) -> str:
    coeffs = np.asarray(coeffs, dtype=float)
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
