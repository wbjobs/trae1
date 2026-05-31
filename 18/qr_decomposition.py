"""
QR分解核心算法模块
实现Householder变换和Givens旋转的QR分解
支持任意阶实矩阵的QR分解
"""

import numpy as np
from dataclasses import dataclass, field
from typing import Tuple, Optional, List
from enum import Enum


class QRMethod(Enum):
    HOUSEHOLDER = "householder"
    GIVENS = "givens"


@dataclass
class QRResult:
    Q: np.ndarray
    R: np.ndarray
    method: QRMethod
    iteration_count: int = 0
    computation_time: float = 0.0
    error: float = 0.0


class QRDecomposition:
    def __init__(self, method: QRMethod = QRMethod.HOUSEHOLDER,
                 tolerance: float = 1e-14):
        self.method = method
        self.tolerance = tolerance
        self.processing_log: List[str] = []

    def decompose(self, matrix: np.ndarray) -> QRResult:
        if self.method == QRMethod.HOUSEHOLDER:
            return self._householder_decomposition(matrix)
        else:
            return self._givens_decomposition(matrix)

    def _householder_decomposition(self, matrix: np.ndarray) -> QRResult:
        n, m = matrix.shape
        A = matrix.copy().astype(np.float64)
        Q = np.eye(n)

        for k in range(min(m, n - 1)):
            x = A[k:, k]
            norm_x = np.linalg.norm(x)

            if norm_x < self.tolerance:
                continue

            e = np.zeros_like(x)
            e[0] = np.sign(x[0]) if x[0] != 0 else 1.0
            v = x + norm_x * e
            v_norm = np.linalg.norm(v)

            if v_norm < self.tolerance:
                continue

            v /= v_norm

            H = np.eye(n)
            H_sub = np.eye(n - k) - 2.0 * np.outer(v, v)
            H[k:, k:] = H_sub

            A = H @ A
            Q = Q @ H.T

        residual = np.linalg.norm(matrix - Q @ A)
        self.processing_log.append(f"Householder QR分解完成, 残差={residual:.6e}")

        return QRResult(
            Q=Q,
            R=A,
            method=QRMethod.HOUSEHOLDER,
            error=residual
        )

    def _givens_decomposition(self, matrix: np.ndarray) -> QRResult:
        n, m = matrix.shape
        A = matrix.copy().astype(np.float64)
        Q = np.eye(n)

        for j in range(min(m, n)):
            for i in range(n - 1, j, -1):
                if abs(A[i, j]) < self.tolerance:
                    continue

                a = A[i - 1, j]
                b = A[i, j]
                r = np.sqrt(a ** 2 + b ** 2)

                c = a / r
                s = b / r

                G = np.eye(n)
                G[i - 1, i - 1] = c
                G[i - 1, i] = s
                G[i, i - 1] = -s
                G[i, i] = c

                A = G @ A
                Q = Q @ G.T

        residual = np.linalg.norm(matrix - Q @ A)
        self.processing_log.append(f"Givens QR分解完成, 残差={residual:.6e}")

        return QRResult(
            Q=Q,
            R=A,
            method=QRMethod.GIVENS,
            error=residual
        )

    def decompose_hessenberg(self, H: np.ndarray) -> Tuple[np.ndarray, np.ndarray]:
        n = H.shape[0]
        Q = np.eye(n)
        R = H.copy()

        for i in range(n - 1):
            a = R[i, i]
            b = R[i + 1, i]
            r = np.sqrt(a ** 2 + b ** 2)

            if r < self.tolerance * 100:
                continue

            c = a / r
            s = b / r

            G = np.array([[c, s], [-s, c]])
            R[i:i + 2, :] = G @ R[i:i + 2, :]
            Q[:, i:i + 2] = Q[:, i:i + 2] @ G.T

        return Q, R

    def single_shift_step(self, H: np.ndarray, shift: float
                          ) -> Tuple[np.ndarray, np.ndarray]:
        n = H.shape[0]
        H_shifted = H.copy().astype(np.float64)
        H_shifted[np.diag_indices(n)] -= shift

        try:
            Q, R = self.decompose_hessenberg(H_shifted)
            H_new = R @ Q
            if np.any(np.isnan(H_new)) or np.any(np.isinf(H_new)):
                raise ValueError("QR step produced NaN or Inf")
            H_new[np.diag_indices(n)] += shift
        except Exception as e:
            H_shifted = H.copy().astype(np.float64)
            H_shifted[np.diag_indices(n)] -= shift + 1e-8
            Q, R = self.decompose_hessenberg(H_shifted)
            H_new = R @ Q
            if np.any(np.isnan(H_new)) or np.any(np.isinf(H_new)):
                H_new = H.copy().astype(np.float64)
                Q = np.eye(n)
            else:
                H_new[np.diag_indices(n)] += shift + 1e-8

        return H_new, Q

    def double_shift_step(self, H: np.ndarray, shift1: complex, shift2: complex
                          ) -> Tuple[np.ndarray, np.ndarray]:
        n = H.shape[0]
        H_new = H.copy()

        for _ in range(2):
            if _ == 0:
                shift = shift1
            else:
                shift = shift2

            H_shifted = H_new.copy()
            H_shifted[np.diag_indices(n)] -= shift.real

            Q, R = self.decompose_hessenberg(H_shifted)
            H_new = R @ Q
            H_new[np.diag_indices(n)] += shift.real

        return H_new, np.eye(n)

    def implicit_qr_step(self, H: np.ndarray, shift: float
                         ) -> Tuple[np.ndarray, np.ndarray]:
        n = H.shape[0]
        Q_total = np.eye(n)
        H_new = H.copy()

        x = H_new[0, 0] ** 2 + H_new[0, 1] * H_new[1, 0] - shift * H_new[0, 0] + shift ** 2
        y = H_new[1, 0] * (H_new[0, 0] + H_new[1, 1] - 2 * shift)
        z = H_new[1, 0] * H_new[2, 1] if n > 2 else 0

        for k in range(n - 1):
            r = np.sqrt(x ** 2 + y ** 2 + z ** 2)

            if r < self.tolerance:
                break

            c = x / r
            s = y / r

            if k < n - 2:
                G = np.eye(n)
                G[k:k + 2, k:k + 2] = np.array([[c, s], [-s, c]])
                H_new = G @ H_new @ G.T
                Q_total = Q_total @ G.T

                x = H_new[k + 1, k]
                y = H_new[k + 2, k]
                z = H_new[k + 3, k] if k + 3 < n else 0
            else:
                G = np.eye(n)
                G[k:k + 2, k:k + 2] = np.array([[c, s], [-s, c]])
                H_new = G @ H_new @ G.T
                Q_total = Q_total @ G.T

        return H_new, Q_total

    def compute_eigenvalues_from_r(self, R: np.ndarray) -> np.ndarray:
        n = R.shape[0]
        eigenvalues = []

        i = 0
        while i < n:
            if i == n - 1 or abs(R[i + 1, i]) < self.tolerance:
                eigenvalues.append(R[i, i])
                i += 1
            else:
                a = R[i, i]
                b = R[i, i + 1]
                c = R[i + 1, i]
                d = R[i + 1, i + 1]

                trace = a + d
                det = a * d - b * c
                discriminant = trace ** 2 - 4 * det

                if discriminant >= 0:
                    sqrt_disc = np.sqrt(discriminant)
                    eigenvalues.append((trace + sqrt_disc) / 2)
                    eigenvalues.append((trace - sqrt_disc) / 2)
                else:
                    sqrt_disc = np.sqrt(abs(discriminant))
                    eigenvalues.append(complex(trace / 2, sqrt_disc / 2))
                    eigenvalues.append(complex(trace / 2, -sqrt_disc / 2))
                i += 2

        return np.array(eigenvalues)

    def get_processing_log(self) -> List[str]:
        return self.processing_log.copy()

    def clear_log(self):
        self.processing_log.clear()
