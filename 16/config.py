"""
全局配置模块
管理迭代精度、最大迭代次数、收敛判据、线搜索、内存限制等全局参数
"""


class SolverConfig:
    """求解器配置类

    支持灵活配置求解器行为，包括收敛判据、线搜索策略、
    内存管理、边界保护等高级参数。
    """

    def __init__(
        self,
        tolerance: float = 1e-8,
        max_iterations: int = 100,
        epsilon: float = 1e-6,
        relaxation_factor: float = 1.0,
        divergence_threshold: float = 1e10,
        enable_logging: bool = True,
        log_file: str = "solver.log",
        use_relative_tolerance: bool = True,
        relative_tolerance: float = 1e-8,
        rms_tolerance: float = 1e-8,
        use_line_search: bool = True,
        line_search_alpha: float = 1e-4,
        line_search_beta: float = 0.5,
        max_line_search_iter: int = 30,
        min_step_size: float = 1e-30,
        max_step_size: float = 1e10,
        enable_damping: bool = True,
        damping_factor: float = 0.5,
        use_adaptive_damping: bool = True,
        max_history_records: int = 0,
        history_sampling_interval: int = 1,
        store_full_arrays: bool = True,
        enable_boundary_check: bool = True,
        variable_lower_bound: float = -1e10,
        variable_upper_bound: float = 1e10,
        singular_jacobian_eps: float = 1e-14,
        jacobian_regularization: float = 1e-10,
        enable_nan_check: bool = True,
        nan_recovery_strategy: str = "damp",
    ):
        self.tolerance = tolerance
        self.max_iterations = max_iterations
        self.epsilon = epsilon
        self.relaxation_factor = relaxation_factor
        self.divergence_threshold = divergence_threshold
        self.enable_logging = enable_logging
        self.log_file = log_file

        self.use_relative_tolerance = use_relative_tolerance
        self.relative_tolerance = relative_tolerance
        self.rms_tolerance = rms_tolerance

        self.use_line_search = use_line_search
        self.line_search_alpha = line_search_alpha
        self.line_search_beta = line_search_beta
        self.max_line_search_iter = max_line_search_iter

        self.min_step_size = min_step_size
        self.max_step_size = max_step_size

        self.enable_damping = enable_damping
        self.damping_factor = damping_factor
        self.use_adaptive_damping = use_adaptive_damping

        self.max_history_records = max_history_records
        self.history_sampling_interval = history_sampling_interval
        self.store_full_arrays = store_full_arrays

        self.enable_boundary_check = enable_boundary_check
        self.variable_lower_bound = variable_lower_bound
        self.variable_upper_bound = variable_upper_bound

        self.singular_jacobian_eps = singular_jacobian_eps
        self.jacobian_regularization = jacobian_regularization

        self.enable_nan_check = enable_nan_check
        self.nan_recovery_strategy = nan_recovery_strategy

    def update(self, **kwargs):
        for key, value in kwargs.items():
            if hasattr(self, key):
                setattr(self, key, value)

    def to_dict(self):
        return {
            "tolerance": self.tolerance,
            "max_iterations": self.max_iterations,
            "epsilon": self.epsilon,
            "relaxation_factor": self.relaxation_factor,
            "divergence_threshold": self.divergence_threshold,
            "use_relative_tolerance": self.use_relative_tolerance,
            "relative_tolerance": self.relative_tolerance,
            "rms_tolerance": self.rms_tolerance,
            "use_line_search": self.use_line_search,
            "enable_damping": self.enable_damping,
            "damping_factor": self.damping_factor,
            "max_history_records": self.max_history_records,
            "enable_boundary_check": self.enable_boundary_check,
            "jacobian_regularization": self.jacobian_regularization,
        }

    def __repr__(self):
        return (
            f"SolverConfig(tolerance={self.tolerance}, "
            f"max_iterations={self.max_iterations}, "
            f"epsilon={self.epsilon})"
        )
