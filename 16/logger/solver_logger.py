"""
日志记录模块
提供求解过程的详细日志记录功能
"""

import logging
import os
from typing import Optional, List
from datetime import datetime
from solver.base import SolverResult, IterationRecord


class SolverLogger:
    """求解器日志记录器

    记录求解过程的关键信息，支持文件和控制台输出，
    便于调试和结果追溯。
    """

    def __init__(self, log_file: Optional[str] = None,
                 console_output: bool = True):
        self.logger = logging.getLogger("NonlinearSolver")
        self.logger.setLevel(logging.DEBUG)
        self.logger.handlers.clear()

        formatter = logging.Formatter(
            "%(asctime)s [%(levelname)s] %(message)s",
            datefmt="%Y-%m-%d %H:%M:%S",
        )

        if console_output:
            console_handler = logging.StreamHandler()
            console_handler.setLevel(logging.INFO)
            console_handler.setFormatter(formatter)
            self.logger.addHandler(console_handler)

        if log_file:
            log_dir = os.path.dirname(log_file)
            if log_dir and not os.path.exists(log_dir):
                os.makedirs(log_dir, exist_ok=True)
            file_handler = logging.FileHandler(log_file, encoding="utf-8")
            file_handler.setLevel(logging.DEBUG)
            file_handler.setFormatter(formatter)
            self.logger.addHandler(file_handler)

    def log_config(self, config):
        self.logger.info("=" * 60)
        self.logger.info("求解器配置")
        self.logger.info(f"  容差 (tolerance): {config.tolerance}")
        self.logger.info(f"  最大迭代次数: {config.max_iterations}")
        self.logger.info(f"  松弛因子: {config.relaxation_factor}")
        self.logger.info(f"  发散阈值: {config.divergence_threshold}")
        self.logger.info("=" * 60)

    def log_equations(self, equations: List[str], variables: List[str]):
        self.logger.info("方程组定义:")
        for i, eq in enumerate(equations):
            self.logger.info(f"  f{i + 1}({', '.join(variables)}) = {eq}")
        self.logger.info(f"变量: {variables}")

    def log_initial_guess(self, x0):
        self.logger.info(f"初始猜测值: {x0}")

    def log_iteration(self, record: IterationRecord):
        x_str = ", ".join(f"{v:.6f}" for v in record.x)
        self.logger.debug(
            f"迭代 {record.iteration:4d} | "
            f"残差范数: {record.residual_norm:.10e} | "
            f"x = [{x_str}]"
        )

    def log_result(self, result: SolverResult):
        self.logger.info("-" * 60)
        self.logger.info("求解结果")
        self.logger.info(f"  状态: {result.status.value}")
        self.logger.info(f"  迭代次数: {result.iterations}")
        self.logger.info(f"  最终残差范数: {result.residual_norm:.10e}")
        if result.solution is not None:
            sol_str = ", ".join(f"{v:.10f}" for v in result.solution)
            self.logger.info(f"  解: [{sol_str}]")
        self.logger.info(f"  消息: {result.message}")
        self.logger.info("-" * 60)

    def log_error(self, error_msg: str):
        self.logger.error(error_msg)

    def log_warning(self, warning_msg: str):
        self.logger.warning(warning_msg)

    def log_info(self, msg: str):
        self.logger.info(msg)

    def log_section(self, title: str):
        self.logger.info(f"\n{'=' * 60}")
        self.logger.info(f"  {title}")
        self.logger.info(f"{'=' * 60}")
