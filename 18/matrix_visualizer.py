"""
矩阵可视化模块
提供矩阵结构、特征值分布、迭代过程等可视化功能
"""

import numpy as np
from typing import List, Optional, Dict, Tuple
from dataclasses import dataclass

try:
    import matplotlib.pyplot as plt
    import matplotlib
    matplotlib.rcParams['font.sans-serif'] = ['SimHei', 'Microsoft YaHei']
    matplotlib.rcParams['axes.unicode_minus'] = False
    HAS_MATPLOTLIB = True
except ImportError:
    HAS_MATPLOTLIB = False

from eigen_solver import IterationData


@dataclass
class VisualizationConfig:
    figsize: Tuple[int, int] = (10, 8)
    dpi: int = 100
    cmap: str = 'viridis'
    show_grid: bool = True
    save_format: str = 'png'
    title_fontsize: int = 14
    label_fontsize: int = 12


class MatrixVisualizer:
    def __init__(self, config: Optional[VisualizationConfig] = None):
        self.config = config or VisualizationConfig()
        self.processing_log: List[str] = []

    def check_availability(self) -> bool:
        return HAS_MATPLOTLIB

    def visualize_matrix(self, matrix: np.ndarray, title: str = "矩阵结构",
                         save_path: Optional[str] = None) -> Optional[object]:
        if not HAS_MATPLOTLIB:
            self.processing_log.append("matplotlib不可用，跳过可视化")
            return None

        fig, axes = plt.subplots(1, 2, figsize=(self.config.figsize[0] * 1.5,
                                                  self.config.figsize[1]))

        im1 = axes[0].imshow(matrix, cmap=self.config.cmap, aspect='auto')
        axes[0].set_title(f'{title} - 热图', fontsize=self.config.title_fontsize)
        axes[0].set_xlabel('列', fontsize=self.config.label_fontsize)
        axes[0].set_ylabel('行', fontsize=self.config.label_fontsize)
        plt.colorbar(im1, ax=axes[0])

        if self.config.show_grid:
            axes[0].grid(True, alpha=0.3)

        mask = np.abs(matrix) > 1e-10
        sparse_matrix = np.where(mask, np.abs(matrix), 0)
        im2 = axes[1].spy(sparse_matrix, markersize=5)
        axes[1].set_title(f'{title} - 稀疏模式', fontsize=self.config.title_fontsize)
        axes[1].set_xlabel('列', fontsize=self.config.label_fontsize)
        axes[1].set_ylabel('行', fontsize=self.config.label_fontsize)

        plt.tight_layout()

        if save_path:
            plt.savefig(save_path, dpi=self.config.dpi, bbox_inches='tight')
            self.processing_log.append(f"矩阵可视化已保存: {save_path}")

        return fig

    def visualize_eigenvalues(self, eigenvalues: np.ndarray,
                              title: str = "特征值分布",
                              matrix: Optional[np.ndarray] = None,
                              save_path: Optional[str] = None) -> Optional[object]:
        if not HAS_MATPLOTLIB:
            self.processing_log.append("matplotlib不可用，跳过可视化")
            return None

        fig, axes = plt.subplots(1, 2, figsize=self.config.figsize)

        real_parts = eigenvalues.real
        imag_parts = eigenvalues.imag

        axes[0].scatter(real_parts, imag_parts, c='blue', marker='o', s=50, alpha=0.7)
        axes[0].axhline(y=0, color='k', linestyle='-', alpha=0.3)
        axes[0].axvline(x=0, color='k', linestyle='-', alpha=0.3)
        axes[0].set_xlabel('实部', fontsize=self.config.label_fontsize)
        axes[0].set_ylabel('虚部', fontsize=self.config.label_fontsize)
        axes[0].set_title(f'{title} - 复平面', fontsize=self.config.title_fontsize)
        if self.config.show_grid:
            axes[0].grid(True, alpha=0.3)

        sorted_ev = np.sort(np.abs(eigenvalues))[::-1]
        axes[1].plot(range(1, len(sorted_ev) + 1), sorted_ev, 'b-o', markersize=5)
        axes[1].set_xlabel('序号', fontsize=self.config.label_fontsize)
        axes[1].set_ylabel('模值', fontsize=self.config.label_fontsize)
        axes[1].set_title(f'{title} - 模值分布', fontsize=self.config.title_fontsize)
        if self.config.show_grid:
            axes[1].grid(True, alpha=0.3)

        if matrix is not None:
            try:
                row_sums = np.max(np.sum(np.abs(matrix), axis=1))
                circle = plt.Circle((0, 0), row_sums, fill=False, color='red',
                                    linestyle='--', alpha=0.5, label='Gershgorin边界')
                axes[0].add_patch(circle)
                axes[0].legend()
            except Exception:
                pass

        plt.tight_layout()

        if save_path:
            plt.savefig(save_path, dpi=self.config.dpi, bbox_inches='tight')
            self.processing_log.append(f"特征值可视化已保存: {save_path}")

        return fig

    def visualize_eigenvectors(self, eigenvectors: np.ndarray,
                               eigenvalues: Optional[np.ndarray] = None,
                               top_k: int = 5,
                               save_path: Optional[str] = None) -> Optional[object]:
        if not HAS_MATPLOTLIB:
            self.processing_log.append("matplotlib不可用，跳过可视化")
            return None

        n = eigenvectors.shape[0]
        k = min(top_k, eigenvectors.shape[1])

        fig, axes = plt.subplots(k, 1, figsize=(10, 3 * k))

        if k == 1:
            axes = [axes]

        for i in range(k):
            vec = eigenvectors[:, i]
            ax = axes[i]

            if np.iscomplexobj(vec):
                ax.plot(range(n), vec.real, 'b-', label='实部', alpha=0.7)
                ax.plot(range(n), vec.imag, 'r--', label='虚部', alpha=0.7)
                ax.legend()
            else:
                ax.plot(range(n), vec.real, 'b-', alpha=0.7)

            title = f'特征向量 {i+1}'
            if eigenvalues is not None and i < len(eigenvalues):
                ev = eigenvalues[i]
                if abs(ev.imag) < 1e-10:
                    title += f' (λ={ev.real:.4f})'
                else:
                    title += f' (λ={ev.real:.4f}+{ev.imag:.4f}j)'

            ax.set_title(title, fontsize=self.config.label_fontsize)
            ax.set_xlabel('分量索引', fontsize=10)
            ax.set_ylabel('值', fontsize=10)
            if self.config.show_grid:
                ax.grid(True, alpha=0.3)

        plt.tight_layout()

        if save_path:
            plt.savefig(save_path, dpi=self.config.dpi, bbox_inches='tight')
            self.processing_log.append(f"特征向量可视化已保存: {save_path}")

        return fig

    def visualize_iteration_history(self, iteration_data: List[IterationData],
                                    title: str = "迭代收敛过程",
                                    save_path: Optional[str] = None) -> Optional[object]:
        if not HAS_MATPLOTLIB:
            self.processing_log.append("matplotlib不可用，跳过可视化")
            return None

        iterations = [d.iteration for d in iteration_data]
        max_off_diagonals = [d.max_off_diagonal for d in iteration_data]
        residuals = [d.residual_norm for d in iteration_data]

        fig, axes = plt.subplots(2, 2, figsize=self.config.figsize)

        axes[0, 0].semilogy(iterations, max_off_diagonals, 'b-', linewidth=2)
        axes[0, 0].set_xlabel('迭代次数', fontsize=self.config.label_fontsize)
        axes[0, 0].set_ylabel('最大次对角元素', fontsize=self.config.label_fontsize)
        axes[0, 0].set_title('次对角元素收敛', fontsize=self.config.title_fontsize)
        if self.config.show_grid:
            axes[0, 0].grid(True, alpha=0.3)

        axes[0, 1].semilogy(iterations, residuals, 'r-', linewidth=2)
        axes[0, 1].set_xlabel('迭代次数', fontsize=self.config.label_fontsize)
        axes[0, 1].set_ylabel('残差范数', fontsize=self.config.label_fontsize)
        axes[0, 1].set_title('残差收敛', fontsize=self.config.title_fontsize)
        if self.config.show_grid:
            axes[0, 1].grid(True, alpha=0.3)

        shifts = [d.shift for d in iteration_data if d.shift is not None]
        if shifts:
            axes[1, 0].plot(shifts, 'g-', linewidth=2)
            axes[1, 0].set_xlabel('迭代次数', fontsize=self.config.label_fontsize)
            axes[1, 0].set_ylabel('位移值', fontsize=self.config.label_fontsize)
            axes[1, 0].set_title('位移策略', fontsize=self.config.title_fontsize)
            if self.config.show_grid:
                axes[1, 0].grid(True, alpha=0.3)

        if iteration_data and iteration_data[-1].eigenvalue_estimate is not None:
            ev_lists = [d.eigenvalue_estimate for d in iteration_data
                       if d.eigenvalue_estimate is not None]
            if ev_lists:
                min_len = min(len(ev) for ev in ev_lists)
                ev_estimates = np.array([ev[:min_len] for ev in ev_lists])
                if len(ev_estimates) > 0 and ev_estimates.shape[1] > 0:
                    for i in range(min(5, ev_estimates.shape[1])):
                        axes[1, 1].plot(range(len(ev_estimates)), ev_estimates[:, i],
                                        linewidth=1, alpha=0.7)
                    axes[1, 1].set_xlabel('迭代次数', fontsize=self.config.label_fontsize)
                    axes[1, 1].set_ylabel('特征值估计', fontsize=self.config.label_fontsize)
                    axes[1, 1].set_title('特征值收敛', fontsize=self.config.title_fontsize)
                    if self.config.show_grid:
                        axes[1, 1].grid(True, alpha=0.3)

        plt.suptitle(title, fontsize=self.config.title_fontsize + 2)
        plt.tight_layout()

        if save_path:
            plt.savefig(save_path, dpi=self.config.dpi, bbox_inches='tight')
            self.processing_log.append(f"迭代过程可视化已保存: {save_path}")

        return fig

    def visualize_error_analysis(self, errors: Dict[str, np.ndarray],
                                 title: str = "误差分析",
                                 save_path: Optional[str] = None) -> Optional[object]:
        if not HAS_MATPLOTLIB:
            self.processing_log.append("matplotlib不可用，跳过可视化")
            return None

        fig, axes = plt.subplots(2, 2, figsize=self.config.figsize)

        idx = 0
        for name, values in errors.items():
            if idx >= 4:
                break
            row, col = divmod(idx, 2)

            if len(values) > 0:
                axes[row, col].bar(range(len(values)), np.abs(values), alpha=0.7)
                axes[row, col].set_title(name, fontsize=self.config.label_fontsize)
                axes[row, col].set_xlabel('索引', fontsize=10)
                axes[row, col].set_ylabel('误差', fontsize=10)
                if self.config.show_grid:
                    axes[row, col].grid(True, alpha=0.3, axis='y')
            idx += 1

        plt.suptitle(title, fontsize=self.config.title_fontsize + 2)
        plt.tight_layout()

        if save_path:
            plt.savefig(save_path, dpi=self.config.dpi, bbox_inches='tight')
            self.processing_log.append(f"误差分析可视化已保存: {save_path}")

        return fig

    def visualize_spectrum_comparison(self, computed: np.ndarray,
                                       reference: np.ndarray,
                                       title: str = "谱对比",
                                       save_path: Optional[str] = None) -> Optional[object]:
        if not HAS_MATPLOTLIB:
            self.processing_log.append("matplotlib不可用，跳过可视化")
            return None

        fig, axes = plt.subplots(1, 2, figsize=self.config.figsize)

        axes[0].scatter(computed.real, computed.imag, c='blue', marker='o',
                        s=50, alpha=0.7, label='计算值')
        axes[0].scatter(reference.real, reference.imag, c='red', marker='x',
                        s=50, alpha=0.7, label='参考值')
        axes[0].axhline(y=0, color='k', linestyle='-', alpha=0.3)
        axes[0].axvline(x=0, color='k', linestyle='-', alpha=0.3)
        axes[0].set_xlabel('实部', fontsize=self.config.label_fontsize)
        axes[0].set_ylabel('虚部', fontsize=self.config.label_fontsize)
        axes[0].set_title(f'{title} - 复平面', fontsize=self.config.title_fontsize)
        axes[0].legend()
        if self.config.show_grid:
            axes[0].grid(True, alpha=0.3)

        diff = np.abs(computed - reference)
        axes[1].bar(range(len(diff)), diff, alpha=0.7, color='green')
        axes[1].set_xlabel('索引', fontsize=self.config.label_fontsize)
        axes[1].set_ylabel('绝对误差', fontsize=self.config.label_fontsize)
        axes[1].set_title(f'{title} - 误差', fontsize=self.config.title_fontsize)
        if self.config.show_grid:
            axes[1].grid(True, alpha=0.3, axis='y')

        plt.tight_layout()

        if save_path:
            plt.savefig(save_path, dpi=self.config.dpi, bbox_inches='tight')
            self.processing_log.append(f"谱对比可视化已保存: {save_path}")

        return fig

    def create_summary_dashboard(self, matrix: np.ndarray,
                                  eigenvalues: np.ndarray,
                                  eigenvectors: Optional[np.ndarray] = None,
                                  iteration_data: Optional[List[IterationData]] = None,
                                  errors: Optional[Dict[str, np.ndarray]] = None,
                                  save_path: Optional[str] = None) -> Optional[object]:
        if not HAS_MATPLOTLIB:
            self.processing_log.append("matplotlib不可用，跳过可视化")
            return None

        fig = plt.figure(figsize=(15, 12))
        gs = fig.add_gridspec(3, 3, hspace=0.3, wspace=0.3)

        ax1 = fig.add_subplot(gs[0, 0])
        im = ax1.imshow(matrix, cmap=self.config.cmap, aspect='auto')
        ax1.set_title('矩阵结构', fontsize=12)
        plt.colorbar(im, ax=ax1)

        ax2 = fig.add_subplot(gs[0, 1])
        ax2.scatter(eigenvalues.real, eigenvalues.imag, c='blue', marker='o', s=30, alpha=0.7)
        ax2.axhline(y=0, color='k', linestyle='-', alpha=0.3)
        ax2.axvline(x=0, color='k', linestyle='-', alpha=0.3)
        ax2.set_title('特征值分布', fontsize=12)
        ax2.set_xlabel('实部')
        ax2.set_ylabel('虚部')

        ax3 = fig.add_subplot(gs[0, 2])
        sorted_ev = np.sort(np.abs(eigenvalues))[::-1]
        ax3.plot(range(1, len(sorted_ev) + 1), sorted_ev, 'b-o', markersize=3)
        ax3.set_title('特征值模值', fontsize=12)
        ax3.set_xlabel('序号')

        if eigenvectors is not None:
            ax4 = fig.add_subplot(gs[1, 0])
            vec = eigenvectors[:, 0]
            if np.iscomplexobj(vec):
                ax4.plot(range(len(vec)), vec.real, 'b-', label='实部')
                ax4.plot(range(len(vec)), vec.imag, 'r--', label='虚部')
                ax4.legend(fontsize=8)
            else:
                ax4.plot(range(len(vec)), vec.real, 'b-')
            ax4.set_title('主特征向量', fontsize=12)

        if iteration_data and len(iteration_data) > 0:
            ax5 = fig.add_subplot(gs[1, 1])
            max_offs = [d.max_off_diagonal for d in iteration_data]
            ax5.semilogy(range(len(max_offs)), max_offs, 'b-', linewidth=2)
            ax5.set_title('收敛曲线', fontsize=12)
            ax5.set_xlabel('迭代')

        if errors:
            ax6 = fig.add_subplot(gs[1, 2])
            first_key = list(errors.keys())[0]
            vals = errors[first_key]
            if hasattr(vals, '__len__') and len(vals) > 0:
                vals = np.abs(vals)
                ax6.bar(range(min(len(vals), 20)), vals[:20], alpha=0.7)
            else:
                ax6.bar([0], [np.abs(vals)], alpha=0.7)
            ax6.set_title('误差分布', fontsize=12)

        ax_info = fig.add_subplot(gs[2, :])
        ax_info.axis('off')

        info_text = f"矩阵维度: {matrix.shape[0]}x{matrix.shape[1]}\n"
        info_text += f"非零元素数: {np.count_nonzero(matrix)}\n"
        info_text += f"矩阵秩: {np.linalg.matrix_rank(matrix)}\n"
        info_text += f"条件数: {np.linalg.cond(matrix):.2e}\n"
        info_text += f"特征值数: {len(eigenvalues)}"

        ax_info.text(0.1, 0.5, info_text, fontsize=12,
                     verticalalignment='center', fontfamily='monospace',
                     bbox=dict(boxstyle='round', facecolor='wheat', alpha=0.5))

        if save_path:
            plt.savefig(save_path, dpi=self.config.dpi, bbox_inches='tight')
            self.processing_log.append(f"综合仪表板已保存: {save_path}")

        return fig

    def get_processing_log(self) -> List[str]:
        return self.processing_log.copy()

    def clear_log(self):
        self.processing_log.clear()
