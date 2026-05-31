"""
结果对比分析模块
提供多算法结果对比、性能分析、统计报告生成功能
支持批量结果对比、算法排名和可视化数据生成
"""

import numpy as np
from typing import List, Dict, Optional, Any, Tuple
from dataclasses import dataclass, field
from solver.base import SolverResult, SolverStatus
from validator.error_checker import ErrorChecker, SolutionValidation


@dataclass
class AlgorithmComparison:
    """算法对比数据"""
    method: str
    iterations: int
    residual_norm: float
    status: str
    solve_time: float = 0.0
    efficiency_score: float = 0.0
    validation_score: float = 0.0
    solution: Optional[np.ndarray] = None


@dataclass
class ComparisonReport:
    """对比报告"""
    algorithms: List[AlgorithmComparison] = field(default_factory=list)
    ranking: List[str] = field(default_factory=list)
    best_method: str = ""
    best_score: float = 0.0
    recommendations: List[str] = field(default_factory=list)


class ResultComparator:
    """结果对比分析器

    提供多算法结果对比、性能分析、统计报告生成功能。
    """

    def __init__(self, error_checker: Optional[ErrorChecker] = None):
        self.error_checker = error_checker or ErrorChecker()

    def compare_results(self,
                         results: Dict[str, SolverResult],
                         parser=None,
                         solve_times: Optional[Dict[str, float]] = None
                         ) -> ComparisonReport:
        """对比多个算法的结果"""
        report = ComparisonReport()
        algorithms = []

        for name, result in results.items():
            comp = self._create_comparison(
                name, result, parser,
                solve_times.get(name) if solve_times else None
            )
            algorithms.append(comp)

        report.algorithms = algorithms
        report.ranking = self._rank_algorithms(algorithms)

        if report.ranking:
            report.best_method = report.ranking[0]
            best_comp = next(
                (a for a in algorithms if a.method == report.best_method),
                None
            )
            if best_comp:
                report.best_score = best_comp.efficiency_score

        report.recommendations = self._generate_recommendations(algorithms, report)

        return report

    def _create_comparison(self, name: str, result: SolverResult,
                            parser=None, solve_time: Optional[float] = None
                            ) -> AlgorithmComparison:
        """创建单个算法的对比数据"""
        efficiency = self.error_checker._compute_efficiency_score(result)

        validation_score = 0.0
        solution = result.solution

        if parser is not None and result.solution is not None:
            validation = self.error_checker.validate_solution(
                parser, result.solution
            )
            validation_score = validation.validation_score
            solution = result.solution

        return AlgorithmComparison(
            method=name,
            iterations=result.iterations,
            residual_norm=result.residual_norm,
            status=result.status.value,
            solve_time=solve_time or 0.0,
            efficiency_score=efficiency,
            validation_score=validation_score,
            solution=solution,
        )

    def _rank_algorithms(self, algorithms: List[AlgorithmComparison]) -> List[str]:
        """对算法进行排名"""
        def get_score(alg):
            score = alg.efficiency_score * 0.6 + alg.validation_score * 0.4
            if alg.status == "converged":
                score += 20
            return score

        sorted_algorithms = sorted(algorithms, key=get_score, reverse=True)
        return [a.method for a in sorted_algorithms]

    def _generate_recommendations(self, algorithms, report) -> List[str]:
        """生成建议"""
        recommendations = []

        converged = [a for a in algorithms if a.status == "converged"]
        if not converged:
            recommendations.append("没有算法收敛，建议增加迭代次数或调整初始猜测")

        fast = min(algorithms, key=lambda a: a.iterations) if algorithms else None
        if fast and fast.status == "converged":
            recommendations.append(f"{fast.method} 收敛最快，仅需 {fast.iterations} 次迭代")

        accurate = max(algorithms, key=lambda a: a.validation_score) if algorithms else None
        if accurate and accurate.validation_score > 0:
            recommendations.append(f"{accurate.method} 解的验证评分最高 ({accurate.validation_score:.2f})")

        return recommendations

    def compare_batch_results(self, batch_results: List) -> ComparisonReport:
        """对比批量求解结果"""
        from solver.batch_solver import BatchResult

        report = ComparisonReport()
        algorithms = []

        for batch_result in batch_results:
            if batch_result.result:
                comp = AlgorithmComparison(
                    method=f"{batch_result.item.name}_{batch_result.method}",
                    iterations=batch_result.result.iterations,
                    residual_norm=batch_result.result.residual_norm,
                    status=batch_result.result.status.value,
                    solve_time=batch_result.solve_time,
                    efficiency_score=self.error_checker._compute_efficiency_score(
                        batch_result.result
                    ),
                    solution=batch_result.result.solution,
                )
                algorithms.append(comp)

        report.algorithms = algorithms
        report.ranking = self._rank_algorithms(algorithms)

        if report.ranking:
            report.best_method = report.ranking[0]

        return report

    def generate_comparison_table(self, report: ComparisonReport) -> str:
        """生成对比表格"""
        lines = []
        lines.append("=" * 80)
        lines.append("  算法对比分析报告")
        lines.append("=" * 80)

        header = f"  {'方法':<20s} {'迭代':<8s} {'残差范数':<16s} {'状态':<15s} {'评分':<10s}"
        lines.append(header)
        lines.append("-" * 80)

        for alg in sorted(
            report.algorithms,
            key=lambda a: report.ranking.index(a.method)
            if a.method in report.ranking else 999
        ):
            lines.append(
                f"  {alg.method:<20s} {alg.iterations:<8d} "
                f"{alg.residual_norm:<16.6e} {alg.status:<15s} "
                f"{alg.efficiency_score:<10.2f}"
            )

        lines.append("-" * 80)

        if report.best_method:
            lines.append(f"\n  最优算法: {report.best_method}")
            lines.append(f"  最优评分: {report.best_score:.2f}")

        if report.recommendations:
            lines.append(f"\n  建议:")
            for rec in report.recommendations:
                lines.append(f"    - {rec}")

        lines.append("\n" + "=" * 80)
        return "\n".join(lines)

    def generate_statistics(self, results: Dict[str, SolverResult]) -> Dict[str, Any]:
        """生成统计数据"""
        stats = {
            "total_algorithms": len(results),
            "converged_count": 0,
            "failed_count": 0,
            "avg_iterations": 0.0,
            "min_iterations": float('inf'),
            "max_iterations": 0,
            "avg_residual": 0.0,
            "min_residual": float('inf'),
            "convergence_rate": 0.0,
        }

        iterations = []
        residuals = []

        for name, result in results.items():
            if result.status == SolverStatus.CONVERGED:
                stats["converged_count"] += 1
            else:
                stats["failed_count"] += 1

            iterations.append(result.iterations)
            residuals.append(result.residual_norm)

        if iterations:
            stats["avg_iterations"] = float(np.mean(iterations))
            stats["min_iterations"] = min(iterations)
            stats["max_iterations"] = max(iterations)

        if residuals:
            stats["avg_residual"] = float(np.mean(residuals))
            stats["min_residual"] = min(residuals)

        total = len(results)
        if total > 0:
            stats["convergence_rate"] = stats["converged_count"] / total

        return stats

    def compare_solutions(self, solutions: Dict[str, np.ndarray],
                          parser) -> Dict[str, Any]:
        """对比多个解的质量"""
        validations = {}
        for name, sol in solutions.items():
            validations[name] = self.error_checker.validate_solution(parser, sol)

        best_name, best_sol, best_val = self.error_checker.select_best_solution(
            solutions, parser
        )

        return {
            "validations": {k: v.__dict__ for k, v in validations.items()},
            "best_solution_name": best_name,
            "best_solution": best_sol.tolist() if best_sol is not None else None,
            "best_validation": best_val.__dict__ if best_val else None,
            "ranking": sorted(
                validations.keys(),
                key=lambda k: validations[k].validation_score,
                reverse=True,
            ),
        }

    def export_comparison_data(self, report: ComparisonReport,
                                filepath: str) -> None:
        """导出对比数据到CSV"""
        import csv
        import os

        os.makedirs(os.path.dirname(filepath) or ".", exist_ok=True)

        with open(filepath, "w", newline="", encoding="utf-8") as f:
            writer = csv.writer(f)
            writer.writerow([
                "method", "iterations", "residual_norm", "status",
                "solve_time", "efficiency_score", "validation_score"
            ])

            for alg in report.algorithms:
                writer.writerow([
                    alg.method,
                    alg.iterations,
                    alg.residual_norm,
                    alg.status,
                    alg.solve_time,
                    alg.efficiency_score,
                    alg.validation_score,
                ])

    def create_visualization_data(self, report: ComparisonReport
                                  ) -> Dict[str, Any]:
        """创建可视化数据"""
        methods = [a.method for a in report.algorithms]
        iterations = [a.iterations for a in report.algorithms]
        residuals = [a.residual_norm for a in report.algorithms]
        scores = [a.efficiency_score for a in report.algorithms]

        return {
            "methods": methods,
            "iterations": iterations,
            "residuals": residuals,
            "scores": scores,
            "ranking": report.ranking,
            "best_method": report.best_method,
        }
