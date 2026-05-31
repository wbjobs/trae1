const Config = {
    stepSize: 0.01,
    minStepSize: 1e-10,
    maxStepSize: 0.1,
    tolerance: 1e-8,
    maxIterations: 10000,
    methods: {
        EULER: 'euler',
        EULER_IMPROVED: 'euler_improved',
        RK4: 'rk4',
        RK45: 'rk45',
        RKF45: 'rkf45'
    },
    defaultConfig: {
        stepSize: 0.01,
        tolerance: 1e-8,
        method: 'rk4',
        adaptiveStep: true,
        safetyFactor: 0.9,
        minScale: 0.2,
        maxScale: 5.0
    }
};

if (typeof module !== 'undefined' && module.exports) {
    module.exports = Config;
}
