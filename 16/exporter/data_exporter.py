"""
数据导出模块
支持将求解结果导出为多种格式（JSON、CSV、Excel、TXT）
"""

import json
import csv
import os
from typing import Dict, Any, Optional, List
from datetime import datetime
from solver.base import SolverResult


class DataExporter:
    """数据导出器

    将求解结果和分析数据导出为多种格式，
    便于后续处理和报告生成。
    """

    def __init__(self, output_dir: str = "output"):
        self.output_dir = output_dir
        os.makedirs(output_dir, exist_ok=True)

    def export_json(self, result: SolverResult, filename: str = "result.json",
                    additional_data: Optional[Dict] = None) -> str:
        filepath = os.path.join(self.output_dir, filename)

        data = result.to_dict()
        data["export_time"] = datetime.now().isoformat()
        if additional_data:
            data["additional"] = additional_data

        with open(filepath, 'w', encoding='utf-8') as f:
            json.dump(data, f, indent=2, ensure_ascii=False)

        print(f"JSON 结果已导出至: {filepath}")
        return filepath

    def export_csv(self, result: SolverResult,
                   filename: str = "iterations.csv") -> str:
        filepath = os.path.join(self.output_dir, filename)

        with open(filepath, 'w', newline='', encoding='utf-8') as f:
            if not result.history:
                writer = csv.writer(f)
                writer.writerow(["无迭代历史"])
                return filepath

            n_vars = len(result.history[0].x)
            n_eq = len(result.history[0].residual)

            header = ["iteration"]
            header += [f"x{i + 1}" for i in range(n_vars)]
            header += [f"residual_{i + 1}" for i in range(n_eq)]
            header += ["residual_norm", "step_norm"]

            writer = csv.writer(f)
            writer.writerow(header)

            for rec in result.history:
                row = [rec.iteration]
                row += rec.x.tolist()
                row += rec.residual.tolist()
                row += [rec.residual_norm, rec.step_norm]
                writer.writerow(row)

        print(f"CSV 结果已导出至: {filepath}")
        return filepath

    def export_excel(self, result: SolverResult,
                     filename: str = "result.xlsx",
                     analysis_data: Optional[Dict] = None) -> str:
        try:
            import pandas as pd
        except ImportError:
            print("警告: pandas 未安装，无法导出 Excel 格式")
            return ""

        try:
            import openpyxl  # noqa: F401
        except ImportError:
            print("警告: openpyxl 未安装，无法导出 Excel 格式")
            return ""

        filepath = os.path.join(self.output_dir, filename)

        with pd.ExcelWriter(filepath, engine='openpyxl') as writer:
            if result.history:
                n_vars = len(result.history[0].x)
                n_eq = len(result.history[0].residual)

                data = []
                for rec in result.history:
                    row = {"iteration": rec.iteration}
                    for i in range(n_vars):
                        row[f"x{i + 1}"] = rec.x[i]
                    for i in range(n_eq):
                        row[f"residual_{i + 1}"] = rec.residual[i]
                    row["residual_norm"] = rec.residual_norm
                    row["step_norm"] = rec.step_norm
                    data.append(row)

                df = pd.DataFrame(data)
                df.to_excel(writer, sheet_name="迭代历史", index=False)

            summary_data = {
                "状态": [result.status.value],
                "迭代次数": [result.iterations],
                "最终残差范数": [result.residual_norm],
                "消息": [result.message],
            }
            if result.solution is not None:
                for i, val in enumerate(result.solution):
                    summary_data[f"解 x{i + 1}"] = [val]

            df_summary = pd.DataFrame(summary_data)
            df_summary.to_excel(writer, sheet_name="求解摘要", index=False)

            if analysis_data:
                analysis_flat = self._flatten_dict(analysis_data)
                df_analysis = pd.DataFrame([analysis_flat])
                df_analysis.to_excel(writer, sheet_name="误差分析", index=False)

        print(f"Excel 结果已导出至: {filepath}")
        return filepath

    def export_txt(self, result: SolverResult, variable_names: List[str],
                   equations: List[str], filename: str = "result.txt") -> str:
        filepath = os.path.join(self.output_dir, filename)

        with open(filepath, 'w', encoding='utf-8') as f:
            f.write("=" * 70 + "\n")
            f.write("         多元非线性方程组求解结果报告\n")
            f.write("=" * 70 + "\n\n")

            f.write(f"生成时间: {datetime.now().strftime('%Y-%m-%d %H:%M:%S')}\n\n")

            f.write("-" * 70 + "\n")
            f.write("方程组:\n")
            f.write("-" * 70 + "\n")
            for i, eq in enumerate(equations):
                f.write(f"  f{i + 1}({', '.join(variable_names)}) = {eq}\n")
            f.write("\n")

            f.write("-" * 70 + "\n")
            f.write("求解结果摘要:\n")
            f.write("-" * 70 + "\n")
            f.write(f"  状态:         {result.status.value}\n")
            f.write(f"  迭代次数:     {result.iterations}\n")
            f.write(f"  残差范数:     {result.residual_norm:.10e}\n")
            f.write(f"  消息:         {result.message}\n\n")

            if result.solution is not None:
                f.write("  解:\n")
                for i, (name, val) in enumerate(
                    zip(variable_names, result.solution)
                ):
                    f.write(f"    {name} = {val:.10f}\n")
            f.write("\n")

            if result.history:
                f.write("-" * 70 + "\n")
                f.write("迭代历史:\n")
                f.write("-" * 70 + "\n")
                header = f"{'迭代':>6s}"
                for name in variable_names:
                    header += f" {name:>14s}"
                header += f" {'残差范数':>14s} {'步长':>14s}\n"
                f.write(header)

                for rec in result.history:
                    line = f"{rec.iteration:6d}"
                    for val in rec.x:
                        line += f" {val:14.6f}"
                    line += f" {rec.residual_norm:14.6e} {rec.step_norm:14.6e}\n"
                    f.write(line)

            f.write("\n" + "=" * 70 + "\n")
            f.write("                     报告结束\n")
            f.write("=" * 70 + "\n")

        print(f"TXT 报告已导出至: {filepath}")
        return filepath

    def export_all(self, result: SolverResult, variable_names: List[str],
                   equations: List[str], analysis_data: Optional[Dict] = None,
                   prefix: str = "") -> Dict[str, str]:
        exported = {}

        prefix_str = f"{prefix}_" if prefix else ""

        exported["json"] = self.export_json(
            result, f"{prefix_str}result.json", analysis_data
        )
        exported["csv"] = self.export_csv(
            result, f"{prefix_str}iterations.csv"
        )
        exported["excel"] = self.export_excel(
            result, f"{prefix_str}result.xlsx", analysis_data
        )
        exported["txt"] = self.export_txt(
            result, variable_names, equations, f"{prefix_str}result.txt"
        )

        return exported

    def _flatten_dict(self, d: Dict, parent_key: str = '',
                      sep: str = '.') -> Dict:
        items = {}
        for k, v in d.items():
            new_key = f"{parent_key}{sep}{k}" if parent_key else k
            if isinstance(v, dict):
                items.update(self._flatten_dict(v, new_key, sep))
            elif isinstance(v, list):
                items[new_key] = str(v)
            else:
                items[new_key] = v
        return items
