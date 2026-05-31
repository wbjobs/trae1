"""
多算法择优求解模块
实现多算法并行求解、自动选择最优算法、混合策略求解
支持基于问题特征的算法推荐和动态切换
"""

import numpy as np
from typing import List, Dict, Optional, Tuple, Any
from dataclasses import dataclass, field
from enum import Enum

from config import SolverConfig
from parser.equation_parser import EquationParser
from solver.base import SolverResult, SolverStatus, BaseSolver
from solver.newton import NewtonSolver
from solver.quasi_newton import QuasiNewtonSolver
from validator.error_checker import ErrorChecker, SolutionValidation


class SelectionStrategy(Enum):
    """算法选择策略"""
    FIRST_CONVERGED = "first_converged"
    BEST_RESIDUAL = "best_residual"
    BEST_EFFICIENCY = "best_efficiency"
    VOTING = "voting"
    WEIGHTED = "weighted"


@dataclass
class AlgorithmProfile:
    """算法配置文件"""
    name: str
    solver_class: type
    config_overrides: Dict[str, Any] = field(default_factory=dict)
    description: str = ""
    suitable_for: List[str] = field(default_factory=list)
    weight: float = 1.0


@dataclass
class ProblemCharacteristics:
    """问题特征分析"""
    dimension: int
    sparsity: float
    nonlinearity: float
    condition_estimate: float
    is_stiff: bool
    has_boundary: bool
    recommended_methods: List[str]


class MultiAlgorithmSolver:
    """多算法择优求解器

    支持多种算法并行求解，自动选择最优结果，
    根据问题特征推荐合适的算法。
    """

    DEFAULT_ALGORITHMS = [
        AlgorithmProfile(
            name="newton",
            solver_class=NewtonSolver,
            description="牛顿法 - 收敛快，适合光滑问题",
            suitable_for=["smooth", "small_dim", "well_conditioned"],
            weight=1.0,
        ),
        AlgorithmProfile(
            name="newton_damped",
            solver_class=NewtonSolver,
            config_overrides={
                "enable_damping": True,
                "damping_factor": 0.5,
                "use_adaptive_damping": True,
            },
            description="阻尼牛顿法 - 鲁棒性强，适合初值不佳",
            suitable_for=["poor_initial", "nonlinear", "unstable"],
            weight=0.9,
        ),
        AlgorithmProfile(
            name="broyden",
            solver_class=QuasiNewtonSolver,
            config_overrides={"method": "broyden"},
            description="Broyden法 - 无需雅可比，适合大规模",
            suitable_for=["large_scale", "expensive_jacobian"],
            weight=0.8,
        ),
        AlgorithmProfile(
            name="broyden_good",
            solver_class=QuasiNewtonSolver,
            config_overrides={"method": "broyden_good"},
            description="Broyden好法 - 逆雅可比更新稳定",
            suitable_for=["stable_update", "moderate_dim"],
            weight=0.85,
        ),
    ]

    def __init__(self, base_config: Optional[SolverConfig] = None,
                 algorithms: Optional[List[AlgorithmProfile]] = None,
                 selection_strategy: SelectionStrategy = SelectionStrategy.BEST_EFFICIENCY,
                 error_checker: Optional[ErrorChecker] = None):
        self.base_config = base_config or SolverConfig()
        self.algorithms = algorithms or self.DEFAULT_ALGORITHMS
        self.selection_strategy = selection_strategy
        self.error_checker = error_checker or ErrorChecker(
            tolerance=self.base_config.tolerance
        )
        self._all_results: Dict[str, SolverResult] = {}
        self._all_validations: Dict[str, SolutionValidation] = {}

    def analyze_problem(self, parser: EquationParser,
                         x0: np.ndarray) -> ProblemCharacteristics:
        """分析问题特征"""
        n = parser.n_vars
        x_sample = x0.copy()

        try:
            jac = parser.jacobian(x_sample)
            cond = float(np.linalg.cond(jac))
            sparsity = float(np.sum(np.abs(jac) < 1e-10)) / (n * n) if n > 0 else 0.0
        except Exception:
            cond = 1e15
            sparsity = 0.0

        f0 = parser.evaluate(x0)
        f1 = parser.evaluate(x0 + 1e-6)
        nonlinearity = float(np.linalg.norm(f1 - f0) / (np.linalg.norm(f0) + 1e-30))

        is_stiff = cond > 1e8 or nonlinearity > 100
        has_boundary = np.any(np.abs(x0) > 1e6) or np.any(np.abs(f0) > 1e6)

        recommended = self._recommend_methods(n, cond, sparsity, nonlinearity, is_stiff)

        return ProblemCharacteristics(
            dimension=n,
            sparsity=sparsity,
            nonlinearity=nonlinearity,
            condition_estimate=cond,
            is_stiff=is_stiff,
            has_boundary=has_boundary,
            recommended_methods=recommended,
        )

    def _recommend_methods(self, n: int, cond: float,
                            sparsity: float, nonlinearity: float,
                            is_stiff: bool) -> List[str]:
        """根据问题特征推荐算法"""
        recommended = []

        if n <= 10 and cond < 1e8:
            recommended.append("newton")
        elif n <= 50 and cond < 1e10:
            recommended.append("newton_damped")
        else:
            recommended.append("broyden")
            recommended.append("broyden_good")

        if is_stiff:
            recommended.append("newton_damped")

        if sparsity > 0.5:
            recommended.append("broyden")

        return list(dict.fromkeys(recommended))

    def solve_with_algorithm(self, parser: EquationParser,
                              x0: np.ndarray,
                              profile: AlgorithmProfile) -> SolverResult:
        """使用指定算法求解"""
        config = self.base_config
        for key, value in profile.config_overrides.items():
            if hasattr(config, key):
                setattr(config, key, value)

        if profile.solver_class == QuasiNewtonSolver:
            method = profile.config_overrides.get("method", "broyden")
            solver = profile.solver_class(config, method=method)
        else:
            solver = profile.solver_class(config)

        return solver.solve(parser, x0)

    def solve_all(self, parser: EquationParser,
                   x0: np.ndarray,
                   selected_algorithms: Optional[List[str]] = None
                   ) -> Dict[str, SolverResult]:
        """使用所有（或选定）算法求解"""
        results = {}
        algorithms = self.algorithms

        if selected_algorithms:
            algorithms = [
                a for a in self.algorithms if a.name in selected_algorithms
            ]

        for profile in algorithms:
            try:
                result = self.solve_with_algorithm(parser, x0, profile)
                results[profile.name] = result
            except Exception as e:
                results[profile.name] = SolverResult(
                    solution=None,
                    status=SolverStatus.ERROR,
                    iterations=0,
                    residual_norm=float('inf'),
                    message=f"{profile.name} 求解失败: {str(e)}",
                )

        self._all_results = results
        return results

    def select_best(self, results: Dict[str, SolverResult],
                    parser: EquationParser) -> Tuple[str, SolverResult, Dict[str, Any]]:
        """根据选择策略选择最优结果"""
        if not results:
            raise ValueError("没有可用的求解结果")

        validations = {}
        for name, result in results.items():
            if result.solution is not None:
                validations[name] = self.error_checker.validate_solution(
                    parser, result.solution
                )

        self._all_validations = validations

        if self.selection_strategy == SelectionStrategy.FIRST_CONVERGED:
            return self._select_first_converged(results, validations)
        elif self.selection_strategy == SelectionStrategy.BEST_RESIDUAL:
            return self._select_best_residual(results, validations)
        elif self.selection_strategy == SelectionStrategy.BEST_EFFICIENCY:
            return self._select_best_efficiency(results, validations)
        elif self.selection_strategy == SelectionStrategy.VOTING:
            return self._select_voting(results, validations)
        elif self.selection_strategy == SelectionStrategy.WEIGHTED:
            return self._select_weighted(results, validations)
        else:
            return self._select_best_efficiency(results, validations)

    def _select_first_converged(self, results, validations):
        for name, result in results.items():
            if result.status == SolverStatus.CONVERGED:
                return name, result, {"strategy": "first_converged"}
        return self._select_best_residual(results, validations)

    def _select_best_residual(self, results, validations):
        best_name = None
        best_result = None
        best_residual = float('inf')

        for name, result in results.items():
            if result.residual_norm < best_residual:
                best_residual = result.residual_norm
                best_name = name
                best_result = result

        return best_name, best_result, {"strategy": "best_residual"}

    def _select_best_efficiency(self, results, validations):
        best_name = None
        best_result = None
        best_score = -1.0

        for name, result in results.items():
            analysis = self.error_checker.full_analysis(
                _create_dummy_parser(result), result
            )
            score = analysis.get("efficiency_score", 0.0)

            if name in validations:
                score += validations[name].validation_score * 0.5

            if score > best_score:
                best_score = score
                best_name = name
                best_result = result

        return best_name, best_result, {
            "strategy": "best_efficiency",
            "best_score": best_score,
        }

    def _select_voting(self, results, validations):
        scores = {name: 0 for name in results}

        converged_names = [
            name for name, result in results.items()
            if result.status == SolverStatus.CONVERGED
        ]
        if converged_names:
            for name in converged_names:
                scores[name] += 3

        for name, val in validations.items():
            if val.is_valid:
                scores[name] += 2
            if val.residual_norm < self.base_config.tolerance:
                scores[name] += 2

        best_name = max(scores, key=scores.get)
        return best_name, results[best_name], {
            "strategy": "voting",
            "scores": scores,
        }

    def _select_weighted(self, results, validations):
        weighted_scores = {}

        for profile in self.algorithms:
            name = profile.name
            if name not in results:
                continue

            result = results[name]
            weight = profile.weight
            score = 0.0

            if result.status == SolverStatus.CONVERGED:
                score += 50 * weight
            elif result.status == SolverStatus.MAX_ITERATIONS:
                score += 10 * weight

            score += max(0, 50 - result.iterations) * weight

            if name in validations:
                val = validations[name]
                score += val.validation_score * weight * 0.5

            weighted_scores[name] = score

        best_name = max(weighted_scores, key=weighted_scores.get)
        return best_name, results[best_name], {
            "strategy": "weighted",
            "weighted_scores": weighted_scores,
        }

    def solve(self, parser: EquationParser, x0: np.ndarray,
              use_recommendation: bool = True) -> Dict[str, Any]:
        """完整的多算法求解流程"""
        if use_recommendation:
            characteristics = self.analyze_problem(parser, x0)
            selected = characteristics.recommended_methods
        else:
            selected = None

        results = self.solve_all(parser, x0, selected)
        best_name, best_result, selection_info = self.select_best(results, parser)

        return {
            "best_method": best_name,
            "best_result": best_result,
            "all_results": results,
            "all_validations": self._all_validations,
            "selection_info": selection_info,
            "problem_characteristics": (
                self.analyze_problem(parser, x0) if use_recommendation else None
            ),
        }

    def get_comparison_report(self) -> str:
        """生成算法对比报告"""
        lines = []
        lines.append("=" * 70)
        lines.append("  多算法择优求解报告")
        lines.append("=" * 70)

        for profile in self.algorithms:
            name = profile.name
            if name not in self._all_results:
                continue

            result = self._all_results[name]
            lines.append(f"\n  算法: {name}")
            lines.append(f"    描述: {profile.description}")
            lines.append(f"    状态: {result.status.value}")
            lines.append(f"    迭代次数: {result.iterations}")
            lines.append(f"    残差范数: {result.residual_norm:.6e}")
            lines.append(f"    消息: {result.message}")

            if name in self._all_validations:
                val = self._all_validations[name]
                lines.append(f"    验证评分: {val.validation_score:.2f}")
                lines.append(f"    解有效: {'是' if val.is_valid else '否'}")

        lines.append("\n" + "=" * 70)
        return "\n".join(lines)


def _create_dummy_parser(result):
    """创建一个简单的解析器用于full_analysis"""
    return _DummyParser(result)


class _DummyParser:
    """用于效率评分的伪解析器"""
    def __init__(self, result):
        self.result = result

    def evaluate(self, x):
        return np.zeros(len(x)) if x is not None else np.array([])

    def jacobian(self, x):
        return np.eye(len(x)) if x is not None else np.eye(1)

    def check_domain(self, x):
        return True, ""

    @property
    def n_vars(self):
        return len(self.result.solution) if self.result.solution is not None else 0
