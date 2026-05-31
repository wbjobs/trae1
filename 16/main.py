"""
多元非线性方程组迭代求解计算系统
主入口文件 - 提供命令行接口和示例演示

模块结构:
- parser: 方程解析模块
- solver: 迭代算法模块 (牛顿法、Broyden拟牛顿法、多算法择优、批量求解)
- validator: 误差校验模块、解的有效性校验、结果对比分析
- logger: 日志记录模块
- visualizer: 结果可视化模块
- exporter: 数据导出模块

新增功能:
- 解的有效性校验
- 多算法择优求解
- 批量方程组求解
- 计算结果对比分析
- 收敛速度优化
"""

import sys
import argparse
import numpy as np
from typing import List, Optional

from config import SolverConfig
from parser.equation_parser import EquationParser
from solver.newton import NewtonSolver
from solver.quasi_newton import QuasiNewtonSolver
from solver.multi_algorithm import MultiAlgorithmSolver, SelectionStrategy
from solver.batch_solver import BatchSolver, BatchItem, create_batch_items_from_config
from solver.base import SolverResult, SolverStatus
from validator.error_checker import ErrorChecker, SolutionValidation
from validator.result_comparator import ResultComparator
from logger.solver_logger import SolverLogger
from visualizer.plotter import ResultVisualizer
from exporter.data_exporter import DataExporter


class NonlinearSolverSystem:
    """多元非线性方程组求解系统

    整合所有模块，提供完整的求解流程管理。
    支持高维方程组、边界参数、数值不稳定情形，
    以及多算法择优、批量求解和结果对比分析。
    """

    def __init__(self, config: Optional[SolverConfig] = None,
                 enable_logging: bool = True):
        self.config = config or SolverConfig()
        self.logger = SolverLogger(
            log_file=self.config.log_file if enable_logging else None,
            console_output=enable_logging,
        ) if enable_logging else None
        self.error_checker = ErrorChecker(tolerance=self.config.tolerance)
        self.result_comparator = ResultComparator(self.error_checker)
        self.visualizer = ResultVisualizer()
        self.exporter = DataExporter()

    def solve_system(self, equations: List[str], x0: np.ndarray,
                     variable_names: Optional[List[str]] = None,
                     method: str = "newton") -> SolverResult:
        parser = EquationParser(equations, variable_names)

        if self.logger:
            self.logger.log_section("开始求解")
            self.logger.log_config(self.config)
            self.logger.log_equations(parser.equations, parser.variable_names)
            self.logger.log_initial_guess(x0)

        if method.lower() == "newton":
            solver = NewtonSolver(self.config)
        elif method.lower() in ("broyden", "broyden_good", "broyden_bad"):
            solver = QuasiNewtonSolver(self.config, method=method.lower())
        elif method.lower() == "auto":
            return self.solve_auto(equations, x0, variable_names)
        else:
            raise ValueError(
                f"不支持的方法: {method}，可选: newton, broyden, broyden_good, broyden_bad, auto"
            )

        result = solver.solve(parser, np.asarray(x0, dtype=float))

        if self.logger:
            self.logger.log_result(result)
            for rec in result.history:
                self.logger.log_iteration(rec)

        return result

    def solve_auto(self, equations: List[str], x0: np.ndarray,
                   variable_names: Optional[List[str]] = None,
                   selection_strategy: SelectionStrategy = SelectionStrategy.BEST_EFFICIENCY
                   ) -> SolverResult:
        """多算法择优求解"""
        parser = EquationParser(equations, variable_names)

        if self.logger:
            self.logger.log_section("多算法择优求解")
            self.logger.log_config(self.config)
            self.logger.log_equations(parser.equations, parser.variable_names)
            self.logger.log_initial_guess(x0)

        multi_solver = MultiAlgorithmSolver(
            base_config=self.config,
            selection_strategy=selection_strategy,
            error_checker=self.error_checker,
        )

        output = multi_solver.solve(parser, np.asarray(x0, dtype=float))

        if self.logger:
            self.logger.log_result(output["best_result"])
            self.logger.log_info(f"最优算法: {output['best_method']}")

        return output["best_result"]

    def validate_solution(self, equations: List[str],
                          variable_names: List[str],
                          solution: np.ndarray) -> SolutionValidation:
        """验证解的有效性"""
        parser = EquationParser(equations, variable_names)
        return self.error_checker.validate_solution(parser, solution)

    def analyze_result(self, equations: List[str],
                       variable_names: List[str],
                       result: SolverResult) -> dict:
        parser = EquationParser(equations, variable_names)
        analysis = self.error_checker.full_analysis(parser, result)
        return analysis

    def compare_methods(self, equations: List[str],
                        x0: np.ndarray,
                        variable_names: Optional[List[str]] = None,
                        methods: Optional[List[str]] = None
                        ) -> dict:
        """对比多种算法的求解结果"""
        if methods is None:
            methods = ["newton", "broyden"]

        parser = EquationParser(equations, variable_names)
        var_names = parser.variable_names

        all_results = {}
        all_solve_times = {}

        import time
        for method in methods:
            start_time = time.time()
            try:
                result = self.solve_system(equations, x0, var_names, method)
                all_results[method] = result
                all_solve_times[method] = time.time() - start_time
            except Exception as e:
                if self.logger:
                    self.logger.log_error(f"{method} 方法求解失败: {e}")

        comparison = self.result_comparator.compare_results(
            all_results, parser, all_solve_times
        )

        return {
            "comparison": comparison,
            "results": all_results,
            "report": self.result_comparator.generate_comparison_table(comparison),
        }

    def export_results(self, result: SolverResult,
                       variable_names: List[str],
                       equations: List[str],
                       analysis: Optional[dict] = None,
                       prefix: str = ""):
        return self.exporter.export_all(
            result, variable_names, equations, analysis, prefix
        )

    def visualize_results(self, result: SolverResult,
                          variable_names: List[str],
                          output_dir: str = "plots",
                          show: bool = False):
        self.visualizer.plot_all(result, variable_names, output_dir, show)

    def solve_batch(self, items: List[BatchItem],
                    method: str = "auto",
                    progress: bool = True) -> list:
        """批量求解方程组"""
        batch_solver = BatchSolver(
            config=self.config,
            method=method,
            error_checker=self.error_checker,
        )

        def progress_callback(current, total, name, success):
            if progress:
                status = "✓" if success else "✗"
                print(f"  [{current}/{total}] {status} {name}")

        if self.logger:
            self.logger.log_section(f"批量求解 ({len(items)} 个方程组)")

        results = batch_solver.solve_batch(items, progress_callback)

        if progress:
            batch_solver.print_summary()

        return results

    def run_full_pipeline(self, equations: List[str],
                          x0: np.ndarray,
                          variable_names: Optional[List[str]] = None,
                          methods: Optional[List[str]] = None,
                          compare: bool = True,
                          export: bool = True,
                          visualize: bool = True,
                          show_plots: bool = False) -> dict:
        if methods is None:
            methods = ["newton", "broyden"]

        parser = EquationParser(equations, variable_names)
        var_names = parser.variable_names

        all_results = {}
        all_analyses = {}

        for method in methods:
            if self.logger:
                self.logger.log_section(f"使用 {method.upper()} 方法求解")

            try:
                result = self.solve_system(equations, x0, var_names, method)
                analysis = self.analyze_result(equations, var_names, result)

                all_results[method] = result
                all_analyses[method] = analysis

                if export:
                    self.export_results(result, var_names, equations,
                                        analysis, prefix=method)

                if visualize and len(var_names) <= 10:
                    self.visualize_results(
                        result, var_names,
                        output_dir=f"plots/{method}",
                        show=show_plots,
                    )

            except Exception as e:
                if self.logger:
                    self.logger.log_error(f"{method} 方法求解失败: {e}")

        if compare and len(all_results) > 1:
            if self.logger:
                self.logger.log_section("算法对比分析")

            comparison = self.compare_methods(
                equations, x0, var_names, methods
            )
            print(comparison["report"])

            if visualize and len(var_names) <= 10:
                self.visualizer.plot_comparison(
                    all_results,
                    save_path="plots/comparison.png",
                    show=show_plots,
                )

        return {"results": all_results, "analyses": all_analyses}


def run_demo():
    """运行演示示例"""
    print("=" * 70)
    print("  多元非线性方程组迭代求解计算系统")
    print("  (增强版: 多算法择优、批量求解、解的有效性校验)")
    print("=" * 70)

    config = SolverConfig(
        tolerance=1e-10,
        rms_tolerance=1e-10,
        relative_tolerance=1e-10,
        max_iterations=200,
        relaxation_factor=1.0,
        use_line_search=True,
        enable_damping=True,
        damping_factor=0.8,
        use_adaptive_damping=True,
        enable_boundary_check=True,
        enable_nan_check=True,
        nan_recovery_strategy="damp",
        max_history_records=200,
        enable_logging=True,
        log_file="solver.log",
    )

    system = NonlinearSolverSystem(config)

    examples = [
        {
            "name": "示例1: 二维非线性方程组",
            "equations": [
                "x1**2 + x2**2 - 4",
                "x1 * x2 - 1",
            ],
            "x0": [2.0, 0.5],
            "variables": ["x1", "x2"],
        },
        {
            "name": "示例2: 三维非线性方程组",
            "equations": [
                "x**2 + y**2 + z**2 - 6",
                "x + y + z - 3",
                "x**2 - y - 1",
            ],
            "x0": [1.5, 1.0, 0.5],
            "variables": ["x", "y", "z"],
        },
        {
            "name": "示例3: 含三角函数的方程组",
            "equations": [
                "sin(x) + y**2 - 2",
                "x**2 - y - 1",
            ],
            "x0": [1.0, 1.0],
            "variables": ["x", "y"],
        },
        {
            "name": "示例4: 五维非线性方程组（高维测试）",
            "equations": [
                "x1**2 + x2**2 - 1",
                "x2**2 + x3**2 - 1.5",
                "x3**2 + x4**2 - 2",
                "x4**2 + x5**2 - 2.5",
                "x1 + x2 + x3 + x4 + x5 - 3",
            ],
            "x0": [0.5, 0.5, 0.8, 0.9, 0.3],
            "variables": ["x1", "x2", "x3", "x4", "x5"],
        },
        {
            "name": "示例5: 边界附近方程组（边界测试）",
            "equations": [
                "x**2 + y**2 - 1e-10",
                "x - y",
            ],
            "x0": [1e-6, 1e-6],
            "variables": ["x", "y"],
        },
    ]

    for ex in examples:
        print(f"\n{'=' * 70}")
        print(f"  {ex['name']}")
        print(f"{'=' * 70}")

        try:
            system.run_full_pipeline(
                equations=ex["equations"],
                x0=np.array(ex["x0"]),
                variable_names=ex.get("variables"),
                methods=["newton", "broyden"],
                compare=True,
                export=True,
                visualize=True,
                show_plots=False,
            )
        except Exception as e:
            print(f"  求解失败: {e}")
            import traceback
            traceback.print_exc()

    print("\n" + "=" * 70)
    print("  所有示例运行完毕")
    print("  结果已导出至 output/ 目录")
    print("  图表已保存至 plots/ 目录")
    print("  日志已保存至 solver.log")
    print("=" * 70)


def run_auto_demo():
    """多算法择优求解演示"""
    print("\n" + "=" * 70)
    print("  多算法择优求解演示")
    print("=" * 70)

    config = SolverConfig(
        tolerance=1e-10,
        max_iterations=200,
        enable_logging=False,
    )

    system = NonlinearSolverSystem(config, enable_logging=False)

    equations = [
        "x**2 + y**2 - 4",
        "x * y - 1",
    ]
    x0 = np.array([2.0, 0.5])

    print("\n使用 auto 模式自动选择最优算法:")
    result = system.solve_system(equations, x0, method="auto")
    print(f"  状态: {result.status.value}")
    print(f"  迭代: {result.iterations}")
    print(f"  残差: {result.residual_norm:.6e}")
    if result.solution is not None:
        print(f"  解: [{result.solution[0]:.10f}, {result.solution[1]:.10f}]")

    print("\n解的有效性校验:")
    validation = system.validate_solution(equations, ["x", "y"], result.solution)
    print(f"  有效: {'是' if validation.is_valid else '否'}")
    print(f"  残差范数: {validation.residual_norm:.6e}")
    print(f"  验证评分: {validation.validation_score:.2f}")

    print("\n多算法对比:")
    comparison = system.compare_methods(equations, x0, methods=["newton", "broyden", "broyden_good"])
    print(comparison["report"])


def run_batch_demo():
    """批量求解演示"""
    print("\n" + "=" * 70)
    print("  批量方程组求解演示")
    print("=" * 70)

    config = SolverConfig(
        tolerance=1e-10,
        max_iterations=200,
        enable_logging=False,
    )

    system = NonlinearSolverSystem(config, enable_logging=False)

    batch_configs = [
        {
            "name": "eq_2d_1",
            "equations": ["x**2 + y**2 - 4", "x * y - 1"],
            "x0": [2.0, 0.5],
            "variable_names": ["x", "y"],
        },
        {
            "name": "eq_2d_2",
            "equations": ["x**2 - y - 1", "y**2 - x - 1"],
            "x0": [1.5, 1.5],
            "variable_names": ["x", "y"],
        },
        {
            "name": "eq_3d",
            "equations": ["x + y + z - 3", "x**2 + y**2 - 2", "x*z - 1"],
            "x0": [1.0, 1.0, 1.0],
            "variable_names": ["x", "y", "z"],
        },
    ]

    items = create_batch_items_from_config(batch_configs)

    print(f"\n求解 {len(items)} 个方程组:")
    results = system.solve_batch(items, method="auto")

    print("\n" + "=" * 70)


def run_stress_test():
    """压力测试 - 高维方程组和边界情形"""
    print("\n" + "=" * 70)
    print("  压力测试 - 高维方程组和边界情形")
    print("=" * 70)

    config = SolverConfig(
        tolerance=1e-10,
        rms_tolerance=1e-10,
        relative_tolerance=1e-10,
        max_iterations=500,
        use_line_search=True,
        enable_damping=True,
        damping_factor=0.7,
        use_adaptive_damping=True,
        enable_boundary_check=True,
        enable_nan_check=True,
        nan_recovery_strategy="damp",
        max_history_records=100,
        history_sampling_interval=5,
        store_full_arrays=False,
        enable_logging=False,
    )

    system = NonlinearSolverSystem(config, enable_logging=False)

    for dim in [10, 20, 50]:
        print(f"\n--- 测试 {dim} 维方程组 ---")
        true_solution = np.ones(dim) * 0.7
        eqs = []
        vars_list = [f"x{i}" for i in range(1, dim + 1)]
        x0 = true_solution + np.random.randn(dim) * 0.02

        for i in range(dim):
            next_idx = (i + 1) % dim
            rhs = true_solution[i] + 0.1 * true_solution[next_idx]
            eqs.append(f"x{i + 1} + 0.1*x{next_idx + 1} - {rhs:.15f}")

        try:
            result = system.solve_system(eqs, x0, vars_list, method="newton")
            sol = result.solution
            error = float(np.linalg.norm(sol - true_solution)) if sol is not None else float('inf')
            print(f"  维度={dim}, 迭代={result.iterations}, "
                  f"状态={result.status.value}, "
                  f"残差={result.residual_norm:.6e}, "
                  f"解误差={error:.6e}")
        except Exception as e:
            print(f"  维度={dim}, 失败: {e}")

    print("\n--- 边界测试 ---")
    boundary_cases = [
        {
            "name": "奇异雅可比测试",
            "equations": ["x**2 - 1", "x - 1"],
            "x0": [1.0],
            "variables": ["x"],
            "true_solution": np.array([1.0]),
        },
        {
            "name": "多根测试",
            "equations": ["x**3 - x"],
            "x0": [0.5],
            "variables": ["x"],
            "true_solution": np.array([0.0]),
        },
        {
            "name": "近奇异测试",
            "equations": ["x**2 + 1e-8 * x - 1", "y**2 + 1e-8 * y - 1"],
            "x0": [1.0, 1.0],
            "variables": ["x", "y"],
            "true_solution": np.array([1.0, 1.0]),
        },
        {
            "name": "边界参数测试(小值解)",
            "equations": ["x * y - 1e-20", "x + y - 1e-10"],
            "x0": [1e-10, 1e-10],
            "variables": ["x", "y"],
            "true_solution": None,
        },
        {
            "name": "刚性方程组测试",
            "equations": ["1000*x**2 + y**2 - 1001", "x**2 + 1000*y**2 - 1001"],
            "x0": [1.0, 1.0],
            "variables": ["x", "y"],
            "true_solution": np.array([1.0, 1.0]),
        },
    ]

    for case in boundary_cases:
        print(f"\n  {case['name']}:")
        try:
            result = system.solve_system(
                case["equations"],
                np.array(case["x0"]),
                case["variables"],
                method="newton",
            )
            sol = result.solution
            error_info = ""
            if sol is not None and case["true_solution"] is not None:
                error = float(np.linalg.norm(sol - case["true_solution"]))
                error_info = f", 解误差={error:.6e}"
            print(f"    状态={result.status.value}, "
                  f"迭代={result.iterations}, "
                  f"残差={result.residual_norm:.6e}{error_info}")
            if sol is not None:
                sol_str = ", ".join(f"{v:.6f}" for v in sol)
                print(f"    解=[{sol_str}]")
        except Exception as e:
            print(f"    失败: {e}")

    print("\n" + "=" * 70)
    print("  压力测试完成")
    print("=" * 70)


def parse_arguments():
    parser = argparse.ArgumentParser(
        description="多元非线性方程组迭代求解计算系统 (增强版)",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="""
示例:
  python main.py --demo
  python main.py --auto-demo
  python main.py --batch-demo
  python main.py --stress-test
  python main.py --equations "x**2+y**2-4" "x*y-1" --vars x y --x0 2 0.5
  python main.py --equations "x**2+y**2+z**2-6" "x+y+z-3" "x**2-y-1" \\
      --vars x y z --x0 1.5 1 0.5 --method auto
  python main.py --equations "sin(x)+y**2-2" "x**2-y-1" \\
      --vars x y --x0 1 1 --method broyden --max-iter 300
  python main.py --equations "x1**2+x2**2-1" "x2**2+x3**2-2" "x1+x2+x3-1.5" \\
      --vars x1 x2 x3 --x0 0.5 0.5 0.5 --compare
        """,
    )

    parser.add_argument(
        "--demo", action="store_true",
        help="运行内置演示示例",
    )
    parser.add_argument(
        "--auto-demo", action="store_true",
        help="运行多算法择优演示",
    )
    parser.add_argument(
        "--batch-demo", action="store_true",
        help="运行批量求解演示",
    )
    parser.add_argument(
        "--stress-test", action="store_true",
        help="运行压力测试（高维和边界情形）",
    )
    parser.add_argument(
        "--equations", nargs="+",
        help="方程组表达式列表",
    )
    parser.add_argument(
        "--vars", nargs="+",
        help="变量名列表",
    )
    parser.add_argument(
        "--x0", type=float, nargs="+",
        help="初始猜测值",
    )
    parser.add_argument(
        "--method", default="newton",
        choices=["newton", "broyden", "broyden_good", "broyden_bad", "auto"],
        help="求解方法 (默认: newton, auto: 自动选择最优算法)",
    )
    parser.add_argument(
        "--compare", action="store_true",
        help="运行多算法对比",
    )
    parser.add_argument(
        "--tolerance", type=float, default=1e-10,
        help="收敛容差 (默认: 1e-10)",
    )
    parser.add_argument(
        "--max-iter", type=int, default=200,
        help="最大迭代次数 (默认: 200)",
    )
    parser.add_argument(
        "--relaxation", type=float, default=1.0,
        help="松弛因子 (默认: 1.0)",
    )
    parser.add_argument(
        "--no-line-search", action="store_true",
        help="禁用线搜索",
    )
    parser.add_argument(
        "--no-damping", action="store_true",
        help="禁用阻尼策略",
    )
    parser.add_argument(
        "--damping-factor", type=float, default=0.8,
        help="阻尼因子 (默认: 0.8)",
    )
    parser.add_argument(
        "--no-log", action="store_true",
        help="禁用日志记录",
    )
    parser.add_argument(
        "--no-export", action="store_true",
        help="禁用数据导出",
    )
    parser.add_argument(
        "--no-plot", action="store_true",
        help="禁用结果可视化",
    )
    parser.add_argument(
        "--show-plots", action="store_true",
        help="显示图表窗口",
    )
    parser.add_argument(
        "--output-dir", default="output",
        help="导出目录 (默认: output)",
    )
    parser.add_argument(
        "--max-history", type=int, default=0,
        help="最大历史记录数 (0=全部, -1=不记录, >0=限制数量)",
    )
    parser.add_argument(
        "--compact", action="store_true",
        help="紧凑模式：不存储完整数组，节省内存",
    )

    return parser.parse_args()


def main():
    args = parse_arguments()

    if args.stress_test:
        run_stress_test()
        return

    if args.auto_demo:
        run_auto_demo()
        return

    if args.batch_demo:
        run_batch_demo()
        return

    if args.demo:
        run_demo()
        return

    if not args.equations:
        print("提示: 使用 --demo 运行演示示例")
        print("提示: 使用 --auto-demo 运行多算法择优演示")
        print("提示: 使用 --batch-demo 运行批量求解演示")
        print("提示: 使用 --stress-test 运行压力测试")
        print("提示: 使用 --help 查看帮助信息")
        print("\n运行演示模式...")
        run_demo()
        return

    if not args.x0 or len(args.x0) != len(args.equations):
        print("错误: 初始猜测值数量必须与方程数量一致")
        print(f"  方程数量: {len(args.equations)}")
        print(f"  初始值数量: {len(args.x0) if args.x0 else 0}")
        sys.exit(1)

    config = SolverConfig(
        tolerance=args.tolerance,
        rms_tolerance=args.tolerance,
        relative_tolerance=args.tolerance,
        max_iterations=args.max_iter,
        relaxation_factor=args.relaxation,
        use_line_search=not args.no_line_search,
        enable_damping=not args.no_damping,
        damping_factor=args.damping_factor,
        use_adaptive_damping=not args.no_damping,
        enable_boundary_check=True,
        enable_nan_check=True,
        nan_recovery_strategy="damp",
        max_history_records=args.max_history,
        store_full_arrays=not args.compact,
        enable_logging=not args.no_log,
    )

    system = NonlinearSolverSystem(config)

    try:
        if args.compare:
            comparison = system.compare_methods(
                equations=args.equations,
                x0=np.array(args.x0),
                variable_names=args.vars,
                methods=["newton", "broyden", "broyden_good"],
            )
            print(comparison["report"])
        else:
            result = system.solve_system(
                equations=args.equations,
                x0=np.array(args.x0),
                variable_names=args.vars,
                method=args.method,
            )

            print(f"\n求解完成:")
            print(f"  状态: {result.status.value}")
            print(f"  迭代次数: {result.iterations}")
            print(f"  残差范数: {result.residual_norm:.10e}")
            if result.solution is not None:
                var_names = args.vars or [
                    f"x{i + 1}" for i in range(len(result.solution))
                ]
                print("  解:")
                for name, val in zip(var_names, result.solution):
                    print(f"    {name} = {val:.10f}")

            validation = system.validate_solution(
                args.equations,
                args.vars or [f"x{i + 1}" for i in range(len(args.x0))],
                result.solution,
            )
            print(f"\n解的有效性校验:")
            print(f"  有效: {'是' if validation.is_valid else '否'}")
            print(f"  验证评分: {validation.validation_score:.2f}")
            if validation.domain_check_message:
                print(f"  定义域信息: {validation.domain_check_message}")

            analysis = system.analyze_result(
                args.equations,
                args.vars or [f"x{i + 1}" for i in range(len(args.x0))],
                result,
            )

        if not args.no_export:
            try:
                system.exporter.export_all(
                    result if not args.compare else comparison["results"].get("newton"),
                    args.vars or [f"x{i + 1}" for i in range(len(args.x0))],
                    args.equations,
                    analysis if not args.compare else None,
                )
            except Exception as e:
                print(f"数据导出时出现警告: {e}")

        if not args.no_plot and not args.compare:
            try:
                system.visualize_results(
                    result,
                    args.vars or [f"x{i + 1}" for i in range(len(args.x0))],
                    show=args.show_plots,
                )
            except Exception as e:
                print(f"可视化时出现警告: {e}")

    except Exception as e:
        print(f"求解失败: {e}")
        import traceback
        traceback.print_exc()
        sys.exit(1)


if __name__ == "__main__":
    main()
