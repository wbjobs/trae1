"""
结果可视化模块
提供收敛曲线、误差趋势等可视化图表
"""

import numpy as np
import matplotlib.pyplot as plt
import matplotlib.font_manager as fm
from typing import List, Optional, Dict, Any
from solver.base import SolverResult, IterationRecord

_CN_FONT_CANDIDATES = [
    'SimHei', 'Microsoft YaHei', 'SimSun', 'KaiTi', 'FangSong',
    'STHeiti', 'STSong', 'STKaiti', 'STFangsong',
    'PingFang SC', 'Hiragino Sans GB', 'Heiti SC',
    'Arial Unicode MS', 'DejaVu Sans',
]

_available_fonts = set(f.name for f in fm.fontManager.ttflist)
_cn_font = next((f for f in _CN_FONT_CANDIDATES if f in _available_fonts), None)

if _cn_font:
    plt.rcParams['font.sans-serif'] = [_cn_font]
plt.rcParams['axes.unicode_minus'] = False


class ResultVisualizer:
    """结果可视化器

    生成收敛曲线、残差变化图、迭代变量轨迹等可视化图表。
    """

    def __init__(self, style: str = "seaborn-v0_8-whitegrid"):
        try:
            plt.style.use(style)
        except Exception:
            plt.style.use("default")

    def plot_convergence(self, result: SolverResult,
                         save_path: Optional[str] = None,
                         show: bool = True):
        if not result.history:
            return None

        iterations = [rec.iteration for rec in result.history]
        residual_norms = [rec.residual_norm for rec in result.history]

        fig, ax = plt.subplots(figsize=(10, 6))
        ax.semilogy(iterations, residual_norms, 'b-o', markersize=4,
                    label='残差范数')
        ax.axhline(y=1e-8, color='r', linestyle='--', alpha=0.7,
                   label='收敛阈值 (1e-8)')
        ax.set_xlabel('迭代次数', fontsize=12)
        ax.set_ylabel('残差范数 (对数尺度)', fontsize=12)
        ax.set_title(f'收敛曲线 - {result.status.value}', fontsize=14)
        ax.legend(fontsize=10)
        ax.grid(True, alpha=0.3)
        fig.tight_layout()

        if save_path:
            fig.savefig(save_path, dpi=150, bbox_inches='tight')
            print(f"收敛曲线已保存至: {save_path}")

        if show:
            plt.show()
        else:
            plt.close(fig)

        return fig

    def plot_residual_components(self, result: SolverResult,
                                 variable_names: List[str],
                                 save_path: Optional[str] = None,
                                 show: bool = True):
        if not result.history:
            return None

        n_vars = len(result.history[0].residual)
        iterations = [rec.iteration for rec in result.history]

        fig, axes = plt.subplots(1, n_vars, figsize=(5 * n_vars, 5))
        if n_vars == 1:
            axes = [axes]

        for i in range(n_vars):
            residuals = np.array([rec.residual[i] for rec in result.history])
            safe_residuals = np.maximum(np.abs(residuals), 1e-30)
            axes[i].semilogy(iterations, safe_residuals, 'g-o', markersize=3)
            axes[i].set_xlabel('迭代次数')
            name = variable_names[i] if i < len(variable_names) else f'f{i+1}'
            axes[i].set_ylabel(f'|{name}|')
            axes[i].set_title(f'方程 {i + 1} 残差')
            axes[i].grid(True, alpha=0.3)

        fig.suptitle('各方程残差收敛情况', fontsize=14)
        fig.tight_layout()

        if save_path:
            fig.savefig(save_path, dpi=150, bbox_inches='tight')
            print(f"残差分量图已保存至: {save_path}")

        if show:
            plt.show()
        else:
            plt.close(fig)

        return fig

    def plot_variable_trajectory(self, result: SolverResult,
                                 variable_names: List[str],
                                 save_path: Optional[str] = None,
                                 show: bool = True):
        if not result.history:
            return None

        n_vars = len(result.history[0].x)
        iterations = [rec.iteration for rec in result.history]

        fig, ax = plt.subplots(figsize=(10, 6))
        for i in range(n_vars):
            values = [rec.x[i] for rec in result.history]
            name = variable_names[i] if i < len(variable_names) else f'x{i+1}'
            ax.plot(iterations, values, '-o', markersize=3, label=name)

        ax.set_xlabel('迭代次数', fontsize=12)
        ax.set_ylabel('变量值', fontsize=12)
        ax.set_title('变量迭代轨迹', fontsize=14)
        ax.legend(fontsize=10)
        ax.grid(True, alpha=0.3)
        fig.tight_layout()

        if save_path:
            fig.savefig(save_path, dpi=150, bbox_inches='tight')
            print(f"变量轨迹图已保存至: {save_path}")

        if show:
            plt.show()
        else:
            plt.close(fig)

        return fig

    def plot_step_sizes(self, result: SolverResult,
                        save_path: Optional[str] = None,
                        show: bool = True):
        if not result.history:
            return None

        iterations = [rec.iteration for rec in result.history]
        step_norms = np.array([rec.step_norm for rec in result.history])
        safe_steps = np.maximum(step_norms, 1e-30)

        fig, ax = plt.subplots(figsize=(10, 6))
        ax.semilogy(iterations, safe_steps, 'm-s', markersize=4)
        ax.set_xlabel('迭代次数', fontsize=12)
        ax.set_ylabel('步长范数 (对数尺度)', fontsize=12)
        ax.set_title('迭代步长变化', fontsize=14)
        ax.grid(True, alpha=0.3)
        fig.tight_layout()

        if save_path:
            fig.savefig(save_path, dpi=150, bbox_inches='tight')
            print(f"步长图已保存至: {save_path}")

        if show:
            plt.show()
        else:
            plt.close(fig)

        return fig

    def plot_comparison(self, results: Dict[str, SolverResult],
                        save_path: Optional[str] = None,
                        show: bool = True):
        fig, ax = plt.subplots(figsize=(10, 6))

        for name, result in results.items():
            if result.history:
                iterations = [rec.iteration for rec in result.history]
                norms = [rec.residual_norm for rec in result.history]
                ax.semilogy(iterations, norms, '-o', markersize=3, label=name)

        ax.axhline(y=1e-8, color='r', linestyle='--', alpha=0.7,
                   label='收敛阈值')
        ax.set_xlabel('迭代次数', fontsize=12)
        ax.set_ylabel('残差范数 (对数尺度)', fontsize=12)
        ax.set_title('算法收敛对比', fontsize=14)
        ax.legend(fontsize=10)
        ax.grid(True, alpha=0.3)
        fig.tight_layout()

        if save_path:
            fig.savefig(save_path, dpi=150, bbox_inches='tight')
            print(f"对比图已保存至: {save_path}")

        if show:
            plt.show()
        else:
            plt.close(fig)

        return fig

    def plot_all(self, result: SolverResult, variable_names: List[str],
                 output_dir: str = "plots", show: bool = False):
        import os
        os.makedirs(output_dir, exist_ok=True)

        self.plot_convergence(
            result,
            save_path=os.path.join(output_dir, "convergence.png"),
            show=show,
        )
        self.plot_residual_components(
            result, variable_names,
            save_path=os.path.join(output_dir, "residuals.png"),
            show=show,
        )
        self.plot_variable_trajectory(
            result, variable_names,
            save_path=os.path.join(output_dir, "trajectory.png"),
            show=show,
        )
        self.plot_step_sizes(
            result,
            save_path=os.path.join(output_dir, "step_sizes.png"),
            show=show,
        )
        print(f"所有图表已保存至: {output_dir}/")
