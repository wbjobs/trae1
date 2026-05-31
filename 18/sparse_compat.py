"""
稀疏矩阵兼容层
当scipy不可用时，提供基于numpy的稀疏矩阵实现
"""

import numpy as np
from typing import Tuple, Optional, List, Dict, Union

try:
    from scipy import sparse
    HAS_SCIPY = True
except ImportError:
    HAS_SCIPY = False


class SparseMatrix:
    def __init__(self, data: Optional[np.ndarray] = None,
                 indices: Optional[np.ndarray] = None,
                 indptr: Optional[np.ndarray] = None,
                 shape: Tuple[int, int] = (0, 0),
                 format: str = 'csr'):
        self.shape = shape
        self.format = format
        self.data = data if data is not None else np.array([], dtype=np.float64)
        self.indices = indices if indices is not None else np.array([], dtype=np.int64)
        self.indptr = indptr if indptr is not None else np.zeros(shape[0] + 1, dtype=np.int64)

    @property
    def nnz(self) -> int:
        return len(self.data)

    @property
    def T(self):
        return self.transpose()

    def toarray(self) -> np.ndarray:
        if self.format == 'csr':
            return self._csr_to_dense()
        elif self.format == 'csc':
            return self._csc_to_dense()
        else:
            return self._coo_to_dense()

    def _csr_to_dense(self) -> np.ndarray:
        n, m = self.shape
        result = np.zeros((n, m), dtype=np.float64)
        for i in range(n):
            start, end = self.indptr[i], self.indptr[i + 1]
            for j in range(start, end):
                col = self.indices[j]
                result[i, col] = self.data[j]
        return result

    def _csc_to_dense(self) -> np.ndarray:
        return self.T._csr_to_dense()

    def _coo_to_dense(self) -> np.ndarray:
        result = np.zeros(self.shape, dtype=np.float64)
        for i, (row, col, val) in enumerate(zip(self.row, self.col, self.data)):
            result[row, col] = val
        return result

    def transpose(self):
        if self.format == 'csr':
            n, m = self.shape
            new_data = []
            new_indices = []
            new_indptr = np.zeros(m + 1, dtype=np.int64)

            col_counts = np.zeros(m, dtype=np.int64)
            for col in self.indices:
                col_counts[col] += 1

            for j in range(m):
                new_indptr[j + 1] = new_indptr[j] + col_counts[j]

            temp_indptr = new_indptr.copy()
            new_data = np.zeros(self.nnz, dtype=np.float64)
            new_indices = np.zeros(self.nnz, dtype=np.int64)

            for i in range(n):
                for j in range(self.indptr[i], self.indptr[i + 1]):
                    col = self.indices[j]
                    pos = temp_indptr[col]
                    new_data[pos] = self.data[j]
                    new_indices[pos] = i
                    temp_indptr[col] += 1

            return SparseMatrix(
                data=new_data,
                indices=new_indices,
                indptr=new_indptr,
                shape=(m, n),
                format='csr'
            )
        else:
            dense = self.toarray()
            return from_dense(dense.T, format=self.format)

    def getrow(self, i: int):
        if self.format == 'csr':
            start, end = self.indptr[i], self.indptr[i + 1]
            return SparseMatrix(
                data=self.data[start:end].copy(),
                indices=self.indices[start:end].copy(),
                indptr=np.array([0, end - start], dtype=np.int64),
                shape=(1, self.shape[1]),
                format='csr'
            )
        else:
            dense = self.toarray()
            return from_dense(dense[i:i+1, :], format=self.format)

    def sum(self, axis: Optional[int] = None):
        if axis is None:
            return np.sum(self.data)
        elif axis == 1:
            n = self.shape[0]
            result = np.zeros(n, dtype=np.float64)
            if self.format == 'csr':
                for i in range(n):
                    start, end = self.indptr[i], self.indptr[i + 1]
                    result[i] = np.sum(self.data[start:end])
            else:
                dense = self.toarray()
                result = np.sum(dense, axis=1)
            return result
        elif axis == 0:
            return self.T.sum(axis=1)
        return None

    def __sub__(self, other):
        if isinstance(other, SparseMatrix):
            return from_dense(self.toarray() - other.toarray(), format=self.format)
        else:
            return from_dense(self.toarray() - other, format=self.format)

    def __mul__(self, other):
        if isinstance(other, (int, float, complex)):
            return SparseMatrix(
                data=self.data * other,
                indices=self.indices.copy(),
                indptr=self.indptr.copy(),
                shape=self.shape,
                format=self.format
            )
        else:
            return from_dense(self.toarray() * other, format=self.format)

    def __repr__(self):
        return f"SparseMatrix(shape={self.shape}, nnz={self.nnz}, format='{self.format}')"


def issparse(matrix) -> bool:
    if HAS_SCIPY:
        return sparse.issparse(matrix)
    return isinstance(matrix, SparseMatrix)


def from_dense(matrix: np.ndarray, threshold: float = 0.0,
               format: str = 'csr') -> SparseMatrix:
    n, m = matrix.shape
    data = []
    indices = []
    indptr = [0]

    for i in range(n):
        for j in range(m):
            val = matrix[i, j]
            if abs(val) > threshold:
                data.append(val)
                indices.append(j)
        indptr.append(len(data))

    return SparseMatrix(
        data=np.array(data, dtype=np.float64),
        indices=np.array(indices, dtype=np.int64),
        indptr=np.array(indptr, dtype=np.int64),
        shape=(n, m),
        format=format
    )


def random(size: Tuple[int, int], density: float = 0.1,
           format: str = 'csr', random_state: Optional[int] = None
           ) -> SparseMatrix:
    if random_state is not None:
        np.random.seed(random_state)

    n, m = size
    total_elements = n * m
    nnz = int(total_elements * density)

    rows = np.random.randint(0, n, nnz)
    cols = np.random.randint(0, m, nnz)
    data = np.random.randn(nnz)

    dense = np.zeros((n, m), dtype=np.float64)
    for r, c, v in zip(rows, cols, data):
        dense[r, c] = v

    return from_dense(dense, format=format)


sparse_random = random


def csr_matrix(matrix) -> SparseMatrix:
    if isinstance(matrix, SparseMatrix):
        return matrix
    if HAS_SCIPY and sparse.issparse(matrix):
        return from_dense(matrix.toarray(), format='csr')
    return from_dense(np.asarray(matrix), format='csr')


def csc_matrix(matrix) -> SparseMatrix:
    if isinstance(matrix, SparseMatrix):
        return matrix
    if HAS_SCIPY and sparse.issparse(matrix):
        return from_dense(matrix.toarray(), format='csc')
    return from_dense(np.asarray(matrix), format='csc')


def coo_matrix(matrix) -> SparseMatrix:
    if isinstance(matrix, SparseMatrix):
        return matrix
    if HAS_SCIPY and sparse.issparse(matrix):
        return from_dense(matrix.toarray(), format='coo')
    return from_dense(np.asarray(matrix), format='coo')


class spmatrix:
    pass
