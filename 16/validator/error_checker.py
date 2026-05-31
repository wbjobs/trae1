"""
误差校验模块
提供收敛性检查、误差分析、结果验证和解的有效性校验功能
支持多算法结果对比、批量验证和综合评分
"""

import numpy as np
from typing import Dict, Any, Optional, List, Tuple
from dataclasses import dataclass, field
from solver.base import SolverResult, IterationRecord, SolverStatus


@dataclass
class SolutionValidation:
    """解的有效性验证结果"""
    is_valid: bool
    residual_norm: float
    residual_rms: float
    residual_max: float
    jacobian_condition: float
    has_nan: bool
    has_inf: bool
    in_domain: bool
    domain_check_message: str = ""
    convergence_order: float = 0.0
    validation_score: float = 0.0


class ErrorChecker:
    """误差校验器

    提供多种误差度量、收敛性分析和解的有效性校验工具，
    支持多算法结果综合评分和批量验证。
    """

    def __init__(self, tolerance: float = 1e-8):
        self.tolerance = tolerance

    def compute_residual_norms(self, residual: np.ndarray) -> Dict[str, float]:
        return {
            "l1": float(np.sum(np.abs(residual))),
            "l2": float(np.linalg.norm(residual)),
            "linf": float(np.max(np.abs(residual))),
            "rms": float(np.sqrt(np.mean(residual ** 2))),
        }

    def compute_relative_error(self, x_current: np.ndarray,
                               x_previous: np.ndarray) -> Optional[float]:
        denom = np.linalg.norm(x_previous)
        if abs(denom) < 1e-15:
            return None
        return float(np.linalg.norm(x_current - x_previous) / denom)

    def check_convergence(self, residual: np.ndarray) -> bool:
        return np.linalg.norm(residual) < self.tolerance

    def analyze_convergence_rate(self, history: List[IterationRecord]) -> Dict[str, Any]:
        if len(history) < 3:
            return {"message": "迭代历史不足，无法分析收敛速率"}

        norms = [rec.residual_norm for rec in history]
        ratios = []
        for i in range(1, len(norms)):
            if norms[i - 1] > 1e-15:
                ratios.append(norms[i] / norms[i - 1])

        if not ratios:
            return {"message": "无法计算收敛比"}

        avg_ratio = float(np.mean(ratios[-min(5, len(ratios)):]))

        if avg_ratio < 0.1:
            rate_type = "快速收敛"
            convergence_order = 2.0
        elif avg_ratio < 0.5:
            rate_type = "中速收敛"
            convergence_order = 1.5
        elif avg_ratio < 0.9:
            rate_type = "线性收敛"
            convergence_order = 1.0
        elif avg_ratio < 1.0:
            rate_type = "缓慢收敛"
            convergence_order = 0.5
        else:
            rate_type = "发散"
            convergence_order = 0.0

        return {
            "convergence_ratio": avg_ratio,
            "convergence_type": rate_type,
            "convergence_order": convergence_order,
            "all_ratios": ratios,
        }

    def verify_solution(self, parser, solution: np.ndarray) -> Dict[str, Any]:
        residual = parser.evaluate(solution)
        norms = self.compute_residual_norms(residual)
        jac = parser.jacobian(solution)

        try:
            cond = float(np.linalg.cond(jac))
        except Exception:
            cond = float('inf')

        return {
            "residual_norms": norms,
            "jacobian_condition_number": cond,
            "is_solution_valid": norms["l2"] < self.tolerance * 100,
            "residual": residual.tolist(),
        }

    def validate_solution(self, parser, solution: np.ndarray) -> SolutionValidation:
        """全面验证解的有效性"""
        is_valid = True
        domain_check_message = ""

        if solution is None:
            return SolutionValidation(
                is_valid=False,
                residual_norm=float('inf'),
                residual_rms=float('inf'),
                residual_max=float('inf'),
                jacobian_condition=float('inf'),
                has_nan=True,
                has_inf=True,
                in_domain=False,
                domain_check_message="解为空",
            )

        has_nan = bool(np.any(np.isnan(solution)))
        has_inf = bool(np.any(np.isinf(solution)))

        if has_nan or has_inf:
            is_valid = False

        in_domain, domain_msg = parser.check_domain(solution)
        if not in_domain:
            is_valid = False
            domain_check_message = domain_msg

        try:
            residual = parser.evaluate(solution)
            residual_norm = float(np.linalg.norm(residual))
            residual_rms = float(np.sqrt(np.mean(residual ** 2)))
            residual_max = float(np.max(np.abs(residual)))

            if residual_norm > self.tolerance * 1000:
                is_valid = False
        except Exception as e:
            residual_norm = float('inf')
            residual_rms = float('inf')
            residual_max = float('inf')
            is_valid = False
            domain_check_message = f"函数求值失败: {str(e)}"

        try:
            jac = parser.jacobian(solution)
            cond = float(np.linalg.cond(jac))
            if cond > 1e15:
                is_valid = False
        except Exception:
            cond = float('inf')
            is_valid = False

        score = self._compute_validation_score(
            residual_norm, cond, has_nan, has_inf, in_domain
        )

        return SolutionValidation(
            is_valid=is_valid,
            residual_norm=residual_norm,
            residual_rms=residual_rms,
            residual_max=residual_max,
            jacobian_condition=cond,
            has_nan=has_nan,
            has_inf=has_inf,
            in_domain=in_domain,
            domain_check_message=domain_check_message,
            validation_score=score,
        )

    def _compute_validation_score(self, residual_norm: float,
                                   condition: float,
                                   has_nan: bool,
                                   has_inf: bool,
                                   in_domain: bool) -> float:
        score = 100.0

        if has_nan or has_inf:
            score -= 50.0
        if not in_domain:
            score -= 30.0

        if residual_norm < self.tolerance:
            score += 20.0
        elif residual_norm < self.tolerance * 10:
            score += 10.0
        elif residual_norm < self.tolerance * 100:
            score += 5.0
        else:
            score -= min(30.0, residual_norm * 10)

        if condition < 1e6:
            score += 10.0
        elif condition < 1e10:
            score += 5.0
        else:
            score -= min(20.0, np.log10(condition + 1))

        return max(0.0, min(100.0, score))

    def compare_solutions(self, solutions: Dict[str, np.ndarray],
                          parser) -> Dict[str, SolutionValidation]:
        """比较多个解的有效性"""
        results = {}
        for name, sol in solutions.items():
            results[name] = self.validate_solution(parser, sol)
        return results

    def select_best_solution(self, solutions: Dict[str, np.ndarray],
                             parser) -> Tuple[str, np.ndarray, SolutionValidation]:
        """选择最优解"""
        validations = self.compare_solutions(solutions, parser)

        best_name = None
        best_sol = None
        best_val = None
        best_score = -1.0

        for name, val in validations.items():
            if val.validation_score > best_score:
                best_score = val.validation_score
                best_name = name
                best_sol = solutions[name]
                best_val = val

        return best_name, best_sol, best_val

    def full_analysis(self, parser, result: SolverResult) -> Dict[str, Any]:
        analysis = {
            "status": result.status.value,
            "iterations": result.iterations,
            "message": result.message,
            "residual_norm": result.residual_norm,
        }

        if result.solution is not None:
            analysis["solution_verification"] = self.verify_solution(
                parser, result.solution
            )
            analysis["solution_validation"] = self.validate_solution(
                parser, result.solution
            ).__dict__

        analysis["convergence_analysis"] = self.analyze_convergence_rate(
            result.history
        )

        if result.history:
            final_residual = result.history[-1].residual
            analysis["final_residual_norms"] = self.compute_residual_norms(
                final_residual
            )

        analysis["efficiency_score"] = self._compute_efficiency_score(result)

        return analysis

    def _compute_efficiency_score(self, result: SolverResult) -> float:
        """计算算法效率评分"""
        score = 100.0

        if result.status == SolverStatus.CONVERGED:
            score += 30.0
        elif result.status == SolverStatus.MAX_ITERATIONS:
            score -= 20.0
        else:
            score -= 50.0

        if result.iterations > 0:
            score -= min(30.0, result.iterations * 0.5)

        if result.residual_norm < self.tolerance:
            score += 20.0
        elif result.residual_norm < self.tolerance * 10:
            score += 10.0
        elif result.residual_norm < self.tolerance * 100:
            score += 5.0
        else:
            score -= min(20.0, np.log10(result.residual_norm + 1))

        return max(0.0, min(100.0, score))

    def compare_results(self, results: Dict[str, SolverResult],
                        parser) -> Dict[str, Any]:
        """比较多个算法的结果"""
        comparison = {
            "methods": list(results.keys()),
            "detailed_comparison": {},
            "best_method": None,
            "best_result": None,
            "ranking": [],
        }

        scores = {}
        for name, result in results.items():
            analysis = self.full_analysis(parser, result)
            comparison["detailed_comparison"][name] = analysis
            scores[name] = analysis.get("efficiency_score", 0.0)

        ranking = sorted(scores.items(), key=lambda x: x[1], reverse=True)
        comparison["ranking"] = ranking

        if ranking:
            best_name = ranking[0][0]
            comparison["best_method"] = best_name
            comparison["best_result"] = results[best_name]

        return comparison

    def batch_validate(self, batch_results: List[Dict[str, Any]],
                       parsers: List) -> List[Dict[str, Any]]:
        """批量验证多个方程组的求解结果"""
        results = []
        for i, batch_item in enumerate(batch_results):
            parser = parsers[i] if i < len(parsers) else parsers[0]
            item_result = {
                "index": i,
                "name": batch_item.get("name", f"batch_{i}"),
            }

            if "result" in batch_item:
                result = batch_item["result"]
                item_result["analysis"] = self.full_analysis(parser, result)
            elif "solution" in batch_item:
                sol = batch_item["solution"]
                item_result["validation"] = self.validate_solution(
                    parser, sol
                ).__dict__

            results.append(item_result)

        return results

    def generate_validation_report(self, validations: Dict[str, SolutionValidation]) -> str:
        """生成验证报告"""
        lines = []
        lines.append("=" * 60)
        lines.append("  解的有效性验证报告")
        lines.append("=" * 60)

        for name, val in validations.items():
            lines.append(f"\n  算法: {name}")
            lines.append(f"    有效: {'是' if val.is_valid else '否'}")
            lines.append(f"    残差范数: {val.residual_norm:.6e}")
            lines.append(f"    残差RMS: {val.residual_rms:.6e}")
            lines.append(f"    最大残差: {val.residual_max:.6e}")
            lines.append(f"    雅可比条件数: {val.jacobian_condition:.6e}")
            lines.append(f"    包含NaN: {'是' if val.has_nan else '否'}")
            lines.append(f"    包含Inf: {'是' if val.has_inf else '否'}")
            lines.append(f"    在定义域内: {'是' if val.in_domain else '否'}")
            if val.domain_check_message:
                lines.append(f"    定义域信息: {val.domain_check_message}")
            lines.append(f"    验证评分: {val.validation_score:.2f}")

        lines.append("\n" + "=" * 60)
        return "\n".join(lines)
