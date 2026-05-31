"""
批量求解模块
支持多个方程组批量求解、结果汇总和性能统计
提供并行求解、进度追踪和批量结果管理功能
"""

import numpy as np
from typing import List, Dict, Optional, Callable, Any
from dataclasses import dataclass, field
from concurrent.futures import ProcessPoolExecutor, ThreadPoolExecutor, as_completed
import time

from config import SolverConfig
from parser.equation_parser import EquationParser
from solver.base import SolverResult, SolverStatus
from solver.newton import NewtonSolver
from solver.quasi_newton import QuasiNewtonSolver
from solver.multi_algorithm import MultiAlgorithmSolver, SelectionStrategy
from validator.error_checker import ErrorChecker


@dataclass
class BatchItem:
    """批量求解项"""
    name: str
    equations: List[str]
    x0: np.ndarray
    variable_names: Optional[List[str]] = None
    true_solution: Optional[np.ndarray] = None
    tags: List[str] = field(default_factory=list)
    metadata: Dict[str, Any] = field(default_factory=dict)


@dataclass
class BatchResult:
    """批量求解结果"""
    item: BatchItem
    result: Optional[SolverResult] = None
    method: str = ""
    error: Optional[str] = None
    solve_time: float = 0.0
    success: bool = False


@dataclass
class BatchSummary:
    """批量求解汇总"""
    total: int = 0
    successful: int = 0
    failed: int = 0
    total_time: float = 0.0
    avg_time: float = 0.0
    success_rate: float = 0.0
    results: List[BatchResult] = field(default_factory=list)
    method_stats: Dict[str, int] = field(default_factory=dict)


class BatchSolver:
    """批量求解器

    支持多个方程组批量求解，提供进度追踪、
    结果汇总和性能统计功能。
    """

    def __init__(self, config: Optional[SolverConfig] = None,
                 method: str = "auto",
                 selection_strategy: SelectionStrategy = SelectionStrategy.BEST_EFFICIENCY,
                 error_checker: Optional[ErrorChecker] = None):
        self.config = config or SolverConfig()
        self.method = method.lower()
        self.selection_strategy = selection_strategy
        self.error_checker = error_checker or ErrorChecker(
            tolerance=self.config.tolerance
        )
        self._results: List[BatchResult] = []

    def solve_single(self, item: BatchItem) -> BatchResult:
        """求解单个方程组"""
        start_time = time.time()
        batch_result = BatchResult(item=item)

        try:
            parser = EquationParser(item.equations, item.variable_names)

            if self.method == "auto":
                multi_solver = MultiAlgorithmSolver(
                    base_config=self.config,
                    selection_strategy=self.selection_strategy,
                    error_checker=self.error_checker,
                )
                solve_output = multi_solver.solve(parser, item.x0)
                batch_result.result = solve_output["best_result"]
                batch_result.method = solve_output["best_method"]
            elif self.method == "newton":
                solver = NewtonSolver(self.config)
                batch_result.result = solver.solve(parser, item.x0)
                batch_result.method = "newton"
            elif self.method in ("broyden", "broyden_good", "broyden_bad"):
                solver = QuasiNewtonSolver(self.config, method=self.method)
                batch_result.result = solver.solve(parser, item.x0)
                batch_result.method = self.method
            else:
                solver = NewtonSolver(self.config)
                batch_result.result = solver.solve(parser, item.x0)
                batch_result.method = "newton"

            batch_result.success = (
                batch_result.result is not None
                and batch_result.result.status == SolverStatus.CONVERGED
            )

        except Exception as e:
            batch_result.error = str(e)
            batch_result.success = False

        batch_result.solve_time = time.time() - start_time
        return batch_result

    def solve_batch(self, items: List[BatchItem],
                    progress_callback: Optional[Callable] = None
                    ) -> List[BatchResult]:
        """批量求解"""
        self._results = []

        for i, item in enumerate(items):
            result = self.solve_single(item)
            self._results.append(result)

            if progress_callback:
                progress_callback(i + 1, len(items), item.name, result.success)

        return self._results

    def solve_batch_parallel(self, items: List[BatchItem],
                              max_workers: int = 2,
                              progress_callback: Optional[Callable] = None
                              ) -> List[BatchResult]:
        """并行批量求解"""
        self._results = []
        completed = 0

        with ThreadPoolExecutor(max_workers=max_workers) as executor:
            future_to_item = {
                executor.submit(self.solve_single, item): item
                for item in items
            }

            for future in as_completed(future_to_item):
                result = future.result()
                self._results.append(result)
                completed += 1

                if progress_callback:
                    progress_callback(
                        completed, len(items),
                        result.item.name, result.success
                    )

        return self._results

    def get_summary(self) -> BatchSummary:
        """获取批量求解汇总"""
        if not self._results:
            return BatchSummary()

        total = len(self._results)
        successful = sum(1 for r in self._results if r.success)
        failed = total - successful
        total_time = sum(r.solve_time for r in self._results)
        avg_time = total_time / total if total > 0 else 0.0

        method_stats: Dict[str, int] = {}
        for r in self._results:
            if r.method:
                method_stats[r.method] = method_stats.get(r.method, 0) + 1

        return BatchSummary(
            total=total,
            successful=successful,
            failed=failed,
            total_time=total_time,
            avg_time=avg_time,
            success_rate=successful / total if total > 0 else 0.0,
            results=self._results.copy(),
            method_stats=method_stats,
        )

    def print_summary(self):
        """打印批量求解汇总"""
        summary = self.get_summary()

        print("\n" + "=" * 70)
        print("  批量求解汇总")
        print("=" * 70)
        print(f"  总数: {summary.total}")
        print(f"  成功: {summary.successful}")
        print(f"  失败: {summary.failed}")
        print(f"  成功率: {summary.success_rate * 100:.1f}%")
        print(f"  总耗时: {summary.total_time:.4f} 秒")
        print(f"  平均耗时: {summary.avg_time:.4f} 秒")

        if summary.method_stats:
            print(f"\n  方法统计:")
            for method, count in summary.method_stats.items():
                print(f"    {method}: {count} 次")

        print("\n  详细结果:")
        print("-" * 70)
        for r in self._results:
            status = "成功" if r.success else "失败"
            print(f"  [{status}] {r.item.name} ({r.method}) "
                  f"- {r.solve_time:.4f}s")
            if r.error:
                print(f"    错误: {r.error}")
            elif r.result:
                print(f"    迭代: {r.result.iterations}, "
                      f"残差: {r.result.residual_norm:.6e}")

        print("=" * 70)

    def export_results(self, output_dir: str = "output"):
        """导出批量求解结果"""
        from exporter.data_exporter import DataExporter
        exporter = DataExporter()

        for r in self._results:
            if r.result and r.result.solution is not None:
                prefix = f"batch_{r.item.name}"
                exporter.export_all(
                    r.result,
                    r.item.variable_names or [
                        f"x{i + 1}" for i in range(len(r.result.solution))
                    ],
                    r.item.equations,
                    prefix=prefix,
                )

    def generate_report(self) -> str:
        """生成详细报告"""
        lines = []
        lines.append("=" * 70)
        lines.append("  批量求解详细报告")
        lines.append("=" * 70)

        summary = self.get_summary()
        lines.append(f"\n总体统计:")
        lines.append(f"  总数: {summary.total}")
        lines.append(f"  成功: {summary.successful}")
        lines.append(f"  失败: {summary.failed}")
        lines.append(f"  成功率: {summary.success_rate * 100:.1f}%")

        lines.append(f"\n各求解结果:")
        for i, r in enumerate(self._results):
            lines.append(f"\n[{i + 1}] {r.item.name}")
            lines.append(f"    方法: {r.method}")
            lines.append(f"    耗时: {r.solve_time:.6f} 秒")
            lines.append(f"    状态: {'成功' if r.success else '失败'}")

            if r.result:
                lines.append(f"    迭代次数: {r.result.iterations}")
                lines.append(f"    残差范数: {r.result.residual_norm:.6e}")
                lines.append(f"    求解状态: {r.result.status.value}")
                lines.append(f"    消息: {r.result.message}")

                if r.result.solution is not None:
                    sol_str = ", ".join(
                        f"{v:.6f}" for v in r.result.solution
                    )
                    lines.append(f"    解: [{sol_str}]")

            if r.error:
                lines.append(f"    错误: {r.error}")

            if r.item.true_solution is not None and r.result and r.result.solution is not None:
                error = np.linalg.norm(r.result.solution - r.item.true_solution)
                lines.append(f"    真实解误差: {error:.6e}")

        lines.append("\n" + "=" * 70)
        return "\n".join(lines)


def create_batch_items_from_config(configs: List[Dict]) -> List[BatchItem]:
    """从配置列表创建批量项"""
    items = []
    for cfg in configs:
        item = BatchItem(
            name=cfg.get("name", f"eq_{len(items)}"),
            equations=cfg["equations"],
            x0=np.array(cfg["x0"], dtype=float),
            variable_names=cfg.get("variable_names"),
            true_solution=(
                np.array(cfg["true_solution"], dtype=float)
                if "true_solution" in cfg else None
            ),
            tags=cfg.get("tags", []),
            metadata=cfg.get("metadata", {}),
        )
        items.append(item)
    return items
