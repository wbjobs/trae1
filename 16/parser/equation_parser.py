"""
方程解析模块
提供多元非线性方程组的符号解析、雅可比矩阵计算与数值安全求值功能
增加边界域检测、数值稳定性处理和高维适配能力
"""

import numpy as np
from sympy import symbols, diff, lambdify, sympify, Symbol, oo, zoo, nan
from typing import List, Callable, Dict, Optional, Tuple


class EquationParser:
    """方程组解析器

    支持将用户定义的方程组解析为可计算的数值函数，
    自动计算雅可比矩阵，提供数值安全求值接口，
    包含边界域检测和数值溢出保护。
    """

    def __init__(self, equations: List[str], variable_names: Optional[List[str]] = None):
        self._equations_str = equations
        self._variable_names = variable_names or self._infer_variables(equations)
        self._n_vars = len(self._variable_names)
        self._symbols = symbols(self._variable_names)
        self._exprs = self._parse_equations(equations)
        self._jacobian_exprs = self._compute_jacobian()
        self._func = self._lambdify_system()
        self._jac_func = self._lambdify_jacobian()
        self._safe_func = self._make_safe_evaluator(self._func)
        self._safe_jac_func = self._make_safe_evaluator(self._jac_func, is_matrix=True)

    def _infer_variables(self, equations: List[str]) -> List[str]:
        import re
        var_set = set()
        reserved = {
            'sin', 'cos', 'tan', 'exp', 'log', 'sqrt', 'abs', 'pi', 'e',
            'asin', 'acos', 'atan', 'sinh', 'cosh', 'tanh', 'E', 'I',
            'sech', 'csch', 'coth', 'asinh', 'acosh', 'atanh',
            'sign', 'heaviside', 'floor', 'ceiling', 'round',
            'ln', 'log10', 'log2', 'log1p', 'expm1',
        }
        for eq in equations:
            tokens = re.findall(r'\b([a-zA-Z_]\w*)\b', eq)
            for token in tokens:
                if token not in reserved:
                    var_set.add(token)
        return sorted(var_set)

    def _parse_equations(self, equations: List[str]) -> list:
        exprs = []
        for eq_str in equations:
            eq_str_clean = eq_str.replace('^', '**')
            expr = sympify(eq_str_clean, locals={str(s): s for s in self._symbols})
            exprs.append(expr)
        return exprs

    def _compute_jacobian(self) -> list:
        jacobian = []
        for expr in self._exprs:
            row = []
            for var in self._symbols:
                partial = diff(expr, var)
                row.append(partial)
            jacobian.append(row)
        return jacobian

    def _lambdify_system(self) -> Callable:
        return lambdify(self._symbols, self._exprs, modules='numpy')

    def _lambdify_jacobian(self) -> Callable:
        return lambdify(self._symbols, self._jacobian_exprs, modules='numpy')

    def _make_safe_evaluator(self, func: Callable, is_matrix: bool = False) -> Callable:
        def safe_eval(*args):
            try:
                with np.errstate(over='ignore', under='ignore',
                                 invalid='ignore', divide='ignore'):
                    result = func(*args)
                if is_matrix:
                    arr = np.array(result, dtype=float)
                else:
                    arr = np.atleast_1d(np.array(result, dtype=float))

                arr = np.nan_to_num(arr, nan=1e10, posinf=1e10, neginf=-1e10)
                return arr
            except Exception:
                if is_matrix:
                    return np.full((self._n_vars, self._n_vars), 1e10)
                else:
                    return np.full(self._n_vars, 1e10)

        return safe_eval

    def evaluate(self, x: np.ndarray) -> np.ndarray:
        return self._safe_func(*x)

    def jacobian(self, x: np.ndarray) -> np.ndarray:
        return self._safe_jac_func(*x)

    def evaluate_with_safety(self, x: np.ndarray) -> Tuple[np.ndarray, bool, str]:
        try:
            f_val = self.evaluate(x)
            if np.any(np.isnan(f_val)) or np.any(np.isinf(f_val)):
                return f_val, False, "函数值包含 NaN/Inf"
            if np.any(np.abs(f_val) > 1e100):
                return f_val, False, "函数值溢出"
            return f_val, True, ""
        except Exception as e:
            return np.full(self._n_vars, 1e10), False, f"求值异常: {str(e)}"

    def jacobian_with_safety(self, x: np.ndarray) -> Tuple[np.ndarray, bool, str]:
        try:
            jac = self.jacobian(x)
            if np.any(np.isnan(jac)) or np.any(np.isinf(jac)):
                return jac, False, "雅可比包含 NaN/Inf"
            if np.any(np.abs(jac) > 1e100):
                return jac, False, "雅可比溢出"
            return jac, True, ""
        except Exception as e:
            return np.eye(self._n_vars) * 1e10, False, f"雅可比求值异常: {str(e)}"

    def check_domain(self, x: np.ndarray) -> Tuple[bool, str]:
        for i, xi in enumerate(x):
            if np.isnan(xi) or np.isinf(xi):
                return False, f"变量 x{i + 1} = {xi} 不是有限实数"

        try:
            f_test = self._func(*x)
            arr = np.array(f_test, dtype=float)
            if np.any(np.isnan(arr)):
                return False, "函数值包含 NaN"
            if np.any(np.isinf(arr)):
                return False, "函数值包含 Inf"
        except ZeroDivisionError:
            return False, "存在除以零的操作"
        except Exception as e:
            return False, f"函数定义域错误: {str(e)}"

        return True, ""

    @property
    def n_vars(self) -> int:
        return self._n_vars

    @property
    def variable_names(self) -> List[str]:
        return self._variable_names.copy()

    @property
    def equations(self) -> List[str]:
        return self._equations_str.copy()

    def __repr__(self) -> str:
        return (
            f"EquationParser(n_vars={self._n_vars}, "
            f"variables={self._variable_names}, "
            f"equations={len(self._equations_str)})"
        )
