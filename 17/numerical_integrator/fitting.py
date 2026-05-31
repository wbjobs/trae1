"""
结果拟合模块
================================

提供积分结果的曲线拟合和插值功能，用于结果分析和可视化。
"""

from typing import Callable, List, Tuple, Optional
from dataclasses import dataclass
import math

from .core_types import IntegralResult, Interval


@dataclass
class FittedPoint:
    """拟合数据点"""
    x: float
    y: float
    weight: float = 1.0


@dataclass
class PolynomialFit:
    """多项式拟合结果"""
    coefficients: List[float]
    degree: int
    r_squared: float
    x_range: Tuple[float, float]

    def evaluate(self, x: float) -> float:
        result = 0.0
        for i, coeff in enumerate(self.coefficients):
            result += coeff * (x ** i)
        return result


@dataclass
class CubicSplineSegment:
    """三次样条段"""
    a: float
    b: float
    c: float
    d: float
    x_start: float
    x_end: float

    def evaluate(self, x: float) -> float:
        dx = x - self.x_start
        return (
            self.a +
            self.b * dx +
            self.c * dx ** 2 +
            self.d * dx ** 3
        )

    def derivative(self, x: float) -> float:
        dx = x - self.x_start
        return (
            self.b +
            2 * self.c * dx +
            3 * self.d * dx ** 2
        )

    def second_derivative(self, x: float) -> float:
        dx = x - self.x_start
        return 2 * self.c + 6 * self.d * dx


@dataclass
class CubicSplineFit:
    """三次样条拟合结果"""
    segments: List[CubicSplineSegment]
    x_points: List[float]
    y_points: List[float]

    def evaluate(self, x: float) -> float:
        segment = self._find_segment(x)
        if segment is None:
            return float('nan')
        return segment.evaluate(x)

    def derivative(self, x: float) -> float:
        segment = self._find_segment(x)
        if segment is None:
            return float('nan')
        return segment.derivative(x)

    def second_derivative(self, x: float) -> float:
        segment = self._find_segment(x)
        if segment is None:
            return float('nan')
        return segment.second_derivative(x)

    def _find_segment(self, x: float) -> Optional[CubicSplineSegment]:
        for seg in self.segments:
            if seg.x_start <= x <= seg.x_end:
                return seg
        if self.segments and x < self.segments[0].x_start:
            return self.segments[0]
        if self.segments and x > self.segments[-1].x_end:
            return self.segments[-1]
        return None


class ResultFitter:
    """
    结果拟合器

    提供多种数据拟合方法：
    - 多项式拟合
    - 三次样条插值
    - 最小二乘拟合
    - 指数拟合
    - 对数拟合
    """

    def __init__(self):
        pass

    def fit_polynomial(
        self,
        x_values: List[float],
        y_values: List[float],
        degree: int = 3
    ) -> PolynomialFit:
        """
        多项式最小二乘拟合

        Args:
            x_values: x坐标列表
            y_values: y坐标列表
            degree: 多项式阶数

        Returns:
            多项式拟合结果
        """
        n = len(x_values)
        if n <= degree:
            degree = n - 1

        A = []
        b = list(y_values)

        for i in range(n):
            row = [x_values[i] ** j for j in range(degree + 1)]
            A.append(row)

        AtA = self._multiply_matrices(self._transpose(A), A)
        Atb = self._multiply_matrix_vector(self._transpose(A), b)

        coefficients = self._solve_linear_system(AtA, Atb)

        y_pred = [self._eval_poly(coefficients, x) for x in x_values]
        r_squared = self._calculate_r_squared(y_values, y_pred)

        return PolynomialFit(
            coefficients=coefficients,
            degree=degree,
            r_squared=r_squared,
            x_range=(min(x_values), max(x_values))
        )

    def fit_cubic_spline(
        self,
        x_values: List[float],
        y_values: List[float],
        natural: bool = True
    ) -> CubicSplineFit:
        """
        三次样条插值

        Args:
            x_values: x坐标列表
            y_values: y坐标列表
            natural: 是否为自然样条（边界二阶导数为0）

        Returns:
            三次样条拟合结果
        """
        n = len(x_values)
        if n < 2:
            return CubicSplineFit(segments=[], x_points=x_values, y_points=y_values)

        h = [x_values[i+1] - x_values[i] for i in range(n-1)]

        alpha = [0.0] * n
        for i in range(1, n-1):
            alpha[i] = (
                3.0 / h[i] * (y_values[i+1] - y_values[i]) -
                3.0 / h[i-1] * (y_values[i] - y_values[i-1])
            )

        c = [0.0] * n
        l = [1.0] * n
        mu = [0.0] * n
        z = [0.0] * n

        for i in range(1, n-1):
            l[i] = 2.0 * (x_values[i+1] - x_values[i-1]) - h[i-1] * mu[i-1]
            if abs(l[i]) < 1e-15:
                l[i] = 1e-15
            mu[i] = h[i] / l[i]
            z[i] = (alpha[i] - h[i-1] * z[i-1]) / l[i]

        b_coeff = [0.0] * (n-1)
        d_coeff = [0.0] * (n-1)

        for j in range(n-2, -1, -1):
            c[j] = z[j] - mu[j] * c[j+1]
            b_coeff[j] = (
                (y_values[j+1] - y_values[j]) / h[j] -
                h[j] * (c[j+1] + 2 * c[j]) / 3.0
            )
            d_coeff[j] = (c[j+1] - c[j]) / (3.0 * h[j])

        segments = []
        for i in range(n-1):
            segments.append(CubicSplineSegment(
                a=y_values[i],
                b=b_coeff[i],
                c=c[i],
                d=d_coeff[i],
                x_start=x_values[i],
                x_end=x_values[i+1]
            ))

        return CubicSplineFit(
            segments=segments,
            x_points=x_values,
            y_points=y_values
        )

    def fit_exponential(
        self,
        x_values: List[float],
        y_values: List[float]
    ) -> Tuple[float, float, float]:
        """
        指数拟合 y = a * exp(b * x)

        Args:
            x_values: x坐标列表
            y_values: y坐标列表

        Returns:
            (a, b, r_squared)
        """
        positive_y = []
        filtered_x = []

        for x, y in zip(x_values, y_values):
            if y > 0:
                positive_y.append(math.log(y))
                filtered_x.append(x)

        if len(positive_y) < 2:
            return 1.0, 0.0, 0.0

        n = len(filtered_x)
        sum_x = sum(filtered_x)
        sum_lny = sum(positive_y)
        sum_x2 = sum(x ** 2 for x in filtered_x)
        sum_xlny = sum(x * ly for x, ly in zip(filtered_x, positive_y))

        denom = n * sum_x2 - sum_x ** 2
        if abs(denom) < 1e-15:
            return 1.0, 0.0, 0.0

        b = (n * sum_xlny - sum_x * sum_lny) / denom
        a_lny = (sum_lny - b * sum_x) / n
        a = math.exp(a_lny)

        y_pred = [a * math.exp(b * x) for x in x_values]
        r_squared = self._calculate_r_squared(y_values, y_pred)

        return a, b, r_squared

    def fit_logarithmic(
        self,
        x_values: List[float],
        y_values: List[float]
    ) -> Tuple[float, float, float]:
        """
        对数拟合 y = a + b * ln(x)

        Args:
            x_values: x坐标列表
            y_values: y坐标列表

        Returns:
            (a, b, r_squared)
        """
        positive_x = []
        filtered_y = []

        for x, y in zip(x_values, y_values):
            if x > 0:
                positive_x.append(math.log(x))
                filtered_y.append(y)

        if len(positive_x) < 2:
            return 0.0, 0.0, 0.0

        n = len(positive_x)
        sum_lnx = sum(positive_x)
        sum_y = sum(filtered_y)
        sum_lnx2 = sum(lx ** 2 for lx in positive_x)
        sum_lnxy = sum(lx * y for lx, y in zip(positive_x, filtered_y))

        denom = n * sum_lnx2 - sum_lnx ** 2
        if abs(denom) < 1e-15:
            return 0.0, 0.0, 0.0

        b = (n * sum_lnxy - sum_lnx * sum_y) / denom
        a = (sum_y - b * sum_lnx) / n

        y_pred = [a + b * math.log(x) if x > 0 else a for x in x_values]
        r_squared = self._calculate_r_squared(y_values, y_pred)

        return a, b, r_squared

    def integrate_fitted_polynomial(
        self,
        poly_fit: PolynomialFit,
        a: float,
        b: float
    ) -> float:
        """
        对拟合多项式进行积分

        Args:
            poly_fit: 多项式拟合结果
            a: 积分下限
            b: 积分上限

        Returns:
            积分值
        """
        integral = 0.0
        for i, coeff in enumerate(poly_fit.coefficients):
            integral += coeff * (b ** (i+1) - a ** (i+1)) / (i+1)
        return integral

    def integrate_spline(
        self,
        spline: CubicSplineFit,
        a: float,
        b: float
    ) -> float:
        """
        对样条插值进行积分

        Args:
            spline: 三次样条拟合结果
            a: 积分下限
            b: 积分上限

        Returns:
            积分值
        """
        total = 0.0

        for seg in spline.segments:
            seg_start = max(a, seg.x_start)
            seg_end = min(b, seg.x_end)

            if seg_start < seg_end:
                integral = self._integrate_cubic_segment(
                    seg, seg_start, seg_end
                )
                total += integral

        return total

    def _integrate_cubic_segment(
        self,
        seg: CubicSplineSegment,
        a: float,
        b: float
    ) -> float:
        a_dx = a - seg.x_start
        b_dx = b - seg.x_start

        def antiderivative(x):
            return (
                seg.a * x +
                seg.b * x ** 2 / 2 +
                seg.c * x ** 3 / 3 +
                seg.d * x ** 4 / 4
            )

        return antiderivative(b_dx) - antiderivative(a_dx)

    def _eval_poly(self, coefficients: List[float], x: float) -> float:
        result = 0.0
        for i, coeff in enumerate(coefficients):
            result += coeff * (x ** i)
        return result

    def _calculate_r_squared(
        self,
        y_true: List[float],
        y_pred: List[float]
    ) -> float:
        if not y_true:
            return 0.0

        mean_y = sum(y_true) / len(y_true)
        ss_res = sum((yt - yp) ** 2 for yt, yp in zip(y_true, y_pred))
        ss_tot = sum((yt - mean_y) ** 2 for yt in y_true)

        if ss_tot < 1e-15:
            return 1.0

        return 1.0 - ss_res / ss_tot

    def _transpose(self, matrix: List[List[float]]) -> List[List[float]]:
        rows = len(matrix)
        cols = len(matrix[0]) if matrix else 0
        return [[matrix[j][i] for j in range(rows)] for i in range(cols)]

    def _multiply_matrices(
        self,
        A: List[List[float]],
        B: List[List[float]]
    ) -> List[List[float]]:
        rows_a = len(A)
        cols_a = len(A[0]) if A else 0
        cols_b = len(B[0]) if B else 0

        result = [[0.0] * cols_b for _ in range(rows_a)]
        for i in range(rows_a):
            for j in range(cols_b):
                for k in range(cols_a):
                    result[i][j] += A[i][k] * B[k][j]
        return result

    def _multiply_matrix_vector(
        self,
        A: List[List[float]],
        v: List[float]
    ) -> List[float]:
        rows = len(A)
        cols = len(A[0]) if A else 0

        result = [0.0] * rows
        for i in range(rows):
            for j in range(cols):
                result[i] += A[i][j] * v[j]
        return result

    def _solve_linear_system(
        self,
        A: List[List[float]],
        b: List[float]
    ) -> List[float]:
        n = len(b)

        for i in range(n):
            max_row = i
            for k in range(i + 1, n):
                if abs(A[k][i]) > abs(A[max_row][i]):
                    max_row = k
            A[i], A[max_row] = A[max_row], A[i]
            b[i], b[max_row] = b[max_row], b[i]

            if abs(A[i][i]) < 1e-15:
                continue

            for k in range(i + 1, n):
                factor = A[k][i] / A[i][i]
                for j in range(i, n):
                    A[k][j] -= factor * A[i][j]
                b[k] -= factor * b[i]

        x = [0.0] * n
        for i in range(n - 1, -1, -1):
            if abs(A[i][i]) < 1e-15:
                x[i] = 0.0
            else:
                x[i] = b[i]
                for j in range(i + 1, n):
                    x[i] -= A[i][j] * x[j]
                x[i] /= A[i][i]

        return x
