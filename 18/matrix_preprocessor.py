"""
矩阵预处理模块
支持稀疏矩阵、矩阵平衡、矩阵缩放等预处理操作
"""

import numpy as np
from sparse_compat import (SparseMatrix, issparse, from_dense, csr_matrix,
                             csc_matrix, coo_matrix, HAS_SCIPY)
if HAS_SCIPY:
    from scipy import sparse
from dataclasses import dataclass, field
from typing import Tuple, Optional, Union, Dict
from enum import Enum


class MatrixType(Enum):
    DENSE = "dense"
    SPARSE_CSR = "sparse_csr"
    SPARSE_CSC = "sparse_csc"
    SPARSE_COO = "sparse_coo"
    SYMMETRIC = "symmetric"
    TRIANGULAR = "triangular"


@dataclass
class MatrixInfo:
    shape: Tuple[int, int]
    matrix_type: MatrixType
    is_sparse: bool
    nnz: int
    density: float
    condition_number: Optional[float] = None
    is_symmetric: bool = False
    is_diagonally_dominant: bool = False


class MatrixPreprocessor:
    def __init__(self, tolerance: float = 1e-14):
        self.tolerance = tolerance
        self.processing_log: list = []

    def analyze_matrix(self, matrix) -> MatrixInfo:
        shape = matrix.shape
        is_sparse = issparse(matrix)

        if is_sparse:
            mtype = MatrixType.SPARSE_CSR
            if HAS_SCIPY and isinstance(matrix, sparse.csr_matrix):
                mtype = MatrixType.SPARSE_CSR
            elif HAS_SCIPY and isinstance(matrix, sparse.csc_matrix):
                mtype = MatrixType.SPARSE_CSC
            elif HAS_SCIPY and isinstance(matrix, sparse.coo_matrix):
                mtype = MatrixType.SPARSE_COO
            
            if not HAS_SCIPY:
                if isinstance(matrix, SparseMatrix):
                    mtype = MatrixType.SPARSE_CSR
                    if matrix.format == 'csc':
                        mtype = MatrixType.SPARSE_CSC
                    elif matrix.format == 'coo':
                        mtype = MatrixType.SPARSE_COO
            
            nnz = matrix.nnz
            density = nnz / (shape[0] * shape[1]) if shape[0] * shape[1] > 0 else 0
        else:
            mtype = MatrixType.DENSE
            nnz = np.count_nonzero(matrix)
            density = nnz / (shape[0] * shape[1]) if shape[0] * shape[1] > 0 else 0

        is_symmetric = self._check_symmetric(matrix)
        is_diag_dominant = self._check_diagonally_dominant(matrix)

        cond = None
        if shape[0] <= 1000 and not is_sparse:
            try:
                cond = np.linalg.cond(matrix)
            except np.linalg.LinAlgError:
                pass

        info = MatrixInfo(
            shape=shape,
            matrix_type=mtype,
            is_sparse=is_sparse,
            nnz=nnz,
            density=density,
            condition_number=cond,
            is_symmetric=is_symmetric,
            is_diagonally_dominant=is_diag_dominant
        )

        self.processing_log.append(f"矩阵分析: {shape[0]}x{shape[1]}, "
                                    f"类型={mtype.value}, 稀疏度={density:.4f}")
        return info

    def to_dense(self, matrix) -> np.ndarray:
        if issparse(matrix):
            self.processing_log.append(f"稀疏矩阵转换为稠密矩阵: {matrix.nnz}个非零元素")
            return matrix.toarray()
        return matrix.copy()

    def to_sparse(self, matrix: np.ndarray, threshold: float = 0.0,
                  format: str = 'csr'):
        if threshold > 0:
            mask = np.abs(matrix) < threshold
            matrix = matrix.copy()
            matrix[mask] = 0

        if format == 'csr':
            self.processing_log.append("转换为CSR稀疏格式")
            return csr_matrix(matrix)
        elif format == 'csc':
            self.processing_log.append("转换为CSC稀疏格式")
            return csc_matrix(matrix)
        else:
            self.processing_log.append("转换为COO稀疏格式")
            return coo_matrix(matrix)

    def balance_matrix(self, matrix: np.ndarray, max_iterations: int = 100) -> np.ndarray:
        n = matrix.shape[0]
        result = matrix.copy()

        for iteration in range(max_iterations):
            changed = False

            for i in range(n):
                row_norm = np.sum(np.abs(result[i, :]))
                col_norm = np.sum(np.abs(result[:, i]))

                if row_norm == 0 or col_norm == 0:
                    continue

                scale = np.sqrt(col_norm / row_norm) if row_norm > 0 else 1.0

                if abs(scale - 1.0) > self.tolerance:
                    result[i, :] /= scale
                    result[:, i] *= scale
                    changed = True

            if not changed:
                break

        self.processing_log.append(f"矩阵平衡: {iteration+1}次迭代后收敛")
        return result

    def permute_to_upper_hessenberg(self, matrix: np.ndarray) -> Tuple[np.ndarray, np.ndarray]:
        n = matrix.shape[0]
        H = matrix.copy().astype(np.float64)
        Q = np.eye(n)

        for k in range(n - 2):
            x = H[k + 1:, k].copy()
            norm_x = np.linalg.norm(x)

            if norm_x < self.tolerance * 100:
                continue

            alpha = -np.sign(x[0]) * norm_x if x[0] != 0 else -norm_x
            x[0] -= alpha
            norm_v = np.linalg.norm(x)

            if norm_v < self.tolerance * 100:
                continue

            x /= norm_v

            H[k + 1:, k:] -= 2.0 * np.outer(x, x @ H[k + 1:, k:])
            H[:, k + 1:] -= 2.0 * np.outer(H[:, k + 1:] @ x, x)
            Q[:, k + 1:] -= 2.0 * np.outer(Q[:, k + 1:] @ x, x)

        self.processing_log.append("矩阵变换为上Hessenberg形式")
        return H, Q

    def scale_matrix(self, matrix: np.ndarray) -> np.ndarray:
        norm = np.linalg.norm(matrix, ord=np.inf)
        if norm > 0:
            result = matrix / norm
            self.processing_log.append(f"矩阵缩放: 缩放因子={1/norm:.6e}")
            return result
        return matrix

    def check_convergence(self, off_diagonal: np.ndarray, tolerance: float) -> bool:
        n = len(off_diagonal)
        for i in range(n - 1):
            if abs(off_diagonal[i]) > tolerance:
                return False
        return True

    def extract_diagonal_blocks(self, matrix: np.ndarray, tolerance: float) -> list:
        n = matrix.shape[0]
        blocks = []
        start = 0

        for i in range(n - 1):
            if abs(matrix[i + 1, i]) < tolerance:
                if i - start >= 0:
                    blocks.append((start, i))
                start = i + 1

        blocks.append((start, n - 1))
        return blocks

    def compute_error_residual(self, matrix: np.ndarray, eigenvalues: np.ndarray,
                               eigenvectors: np.ndarray) -> np.ndarray:
        residuals = np.zeros(len(eigenvalues))
        n = matrix.shape[0]

        for i, (ev, vec) in enumerate(zip(eigenvalues, eigenvectors.T)):
            residual = matrix @ vec - ev * vec
            residuals[i] = np.linalg.norm(residual) / (np.linalg.norm(vec) + self.tolerance)

        return residuals

    def _check_symmetric(self, matrix) -> bool:
        if issparse(matrix):
            if matrix.shape[0] != matrix.shape[1]:
                return False
            diff = matrix - matrix.T
            if hasattr(diff, 'data'):
                return np.all(np.abs(diff.data) < self.tolerance)
            return np.all(np.abs(diff.toarray()) < self.tolerance)
        else:
            if matrix.shape[0] != matrix.shape[1]:
                return False
            return np.allclose(matrix, matrix.T, atol=self.tolerance)

    def _check_diagonally_dominant(self, matrix) -> bool:
        n = matrix.shape[0]
        for i in range(n):
            if issparse(matrix):
                row = matrix.getrow(i)
                row_array = row.toarray().flatten()
                diag = abs(row_array[i])
                off_diag = np.sum(np.abs(row_array)) - diag
            else:
                diag = abs(matrix[i, i])
                off_diag = np.sum(np.abs(matrix[i, :])) - diag
            if diag < off_diag:
                return False
        return True

    def get_processing_log(self) -> list:
        return self.processing_log.copy()

    def clear_log(self):
        self.processing_log.clear()
