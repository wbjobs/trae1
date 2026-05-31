class ErrorSourceAnalyzer {
    constructor(config = {}) {
        this.truncationErrorOrder = config.truncationErrorOrder || {};
        this.roundingErrorThreshold = config.roundingErrorThreshold || 1e-14;
    }

    analyze(result, f, analyticalSolution = null) {
        const analysis = {
            totalError: 0,
            errorSources: {
                truncationError: { value: 0, percentage: 0, description: '截断误差' },
                roundingError: { value: 0, percentage: 0, description: '舍入误差' },
                stabilityError: { value: 0, percentage: 0, description: '稳定性误差' },
                implementationError: { value: 0, percentage: 0, description: '实现误差' }
            },
            errorConvergence: null,
            stepSizeEffect: null,
            recommendations: [],
            reliability: 'high'
        };

        const { y, t, stepSize, method } = result;

        if (analyticalSolution && Array.isArray(t)) {
            const errors = [];
            for (let i = 0; i < t.length; i++) {
                const exact = analyticalSolution(t[i]);
                const computed = Array.isArray(y[i]) ? y[i][0] : y[i];
                errors.push(Math.abs(computed - exact));
            }

            const maxError = Math.max(...errors);
            const meanError = errors.reduce((a, b) => a + b, 0) / errors.length;

            analysis.totalError = maxError;

            analysis.errorSources.truncationError.value = this.estimateTruncationError(method, stepSize, f, t, y);
            analysis.errorSources.roundingError.value = this.estimateRoundingError(y);
            analysis.errorSources.stabilityError.value = this.estimateStabilityError(errors, stepSize, method);
            analysis.errorSources.implementationError.value = this.estimateImplementationError(y, t, f);

            const totalComponents = analysis.errorSources.truncationError.value +
                analysis.errorSources.roundingError.value +
                analysis.errorSources.stabilityError.value +
                analysis.errorSources.implementationError.value;

            if (totalComponents > 0) {
                analysis.errorSources.truncationError.percentage =
                    (analysis.errorSources.truncationError.value / totalComponents) * 100;
                analysis.errorSources.roundingError.percentage =
                    (analysis.errorSources.roundingError.value / totalComponents) * 100;
                analysis.errorSources.stabilityError.percentage =
                    (analysis.errorSources.stabilityError.value / totalComponents) * 100;
                analysis.errorSources.implementationError.percentage =
                    (analysis.errorSources.implementationError.value / totalComponents) * 100;
            }

            analysis.errorConvergence = this.analyzeConvergence(errors, t);
        }

        analysis.stepSizeEffect = this.analyzeStepSizeEffect(stepSize, method);

        analysis.recommendations = this.generateRecommendations(analysis);

        if (analysis.totalError > 0.1) {
            analysis.reliability = 'low';
        } else if (analysis.totalError > 0.01) {
            analysis.reliability = 'medium';
        }

        return analysis;
    }

    estimateTruncationError(method, stepSize, f, t, y) {
        const errorOrders = {
            'euler': 1,
            'heun': 2,
            'improvedEuler': 2,
            'midpoint': 2,
            'rk2': 2,
            'rk3': 3,
            'rk4': 4,
            'rk5': 5,
            'rk45': 4
        };

        const order = errorOrders[method] || 1;

        if (Array.isArray(t) && t.length > 1) {
            let maxDerivative = 0;
            for (let i = 0; i < t.length; i++) {
                const yVal = Array.isArray(y[i]) ? y[i] : [y[i]];
                const fVal = f(t[i], yVal);
                const fMag = Array.isArray(fVal) ?
                    Math.sqrt(fVal.reduce((a, b) => a + b * b, 0)) :
                    Math.abs(fVal);
                if (fMag > maxDerivative) maxDerivative = fMag;
            }

            const C = this.getMethodConstant(method);
            return C * maxDerivative * Math.pow(stepSize, order + 1);
        }

        return Math.pow(stepSize, order);
    }

    getMethodConstant(method) {
        const constants = {
            'euler': 0.5,
            'heun': 1 / 6,
            'improvedEuler': 1 / 6,
            'midpoint': 1 / 6,
            'rk2': 1 / 6,
            'rk3': 1 / 24,
            'rk4': 1 / 120,
            'rk5': 1 / 720,
            'rk45': 1 / 120
        };
        return constants[method] || 1;
    }

    estimateRoundingError(y) {
        if (Array.isArray(y) && y.length > 0) {
            let maxValue = 0;
            for (const val of y) {
                if (Array.isArray(val)) {
                    for (const v of val) {
                        if (Math.abs(v) > maxValue) maxValue = Math.abs(v);
                    }
                } else {
                    if (Math.abs(val) > maxValue) maxValue = Math.abs(val);
                }
            }
            return maxValue * this.roundingErrorThreshold;
        }
        return this.roundingErrorThreshold;
    }

    estimateStabilityError(errors, stepSize, method) {
        if (!Array.isArray(errors) || errors.length < 3) return 0;

        const growth = [];
        for (let i = 1; i < errors.length; i++) {
            if (errors[i - 1] > 1e-15) {
                growth.push(errors[i] / errors[i - 1]);
            }
        }

        if (growth.length === 0) return 0;

        const avgGrowth = growth.reduce((a, b) => a + b, 0) / growth.length;

        if (avgGrowth > 1.1) {
            return (avgGrowth - 1) * errors[errors.length - 1];
        }

        return 0;
    }

    estimateImplementationError(y, t, f) {
        if (!Array.isArray(t) || t.length < 2) return 0;

        let maxInconsistency = 0;
        for (let i = 0; i < t.length - 1; i++) {
            const y0 = Array.isArray(y[i]) ? y[i] : [y[i]];
            const y1 = Array.isArray(y[i + 1]) ? y[i + 1] : [y[i + 1]];
            const dt = t[i + 1] - t[i];

            const f0 = f(t[i], y0);
            const f1 = f(t[i + 1], y1);

            const f0Arr = Array.isArray(f0) ? f0 : [f0];
            const f1Arr = Array.isArray(f1) ? f1 : [f1];

            for (let j = 0; j < Math.min(y0.length, f0Arr.length); j++) {
                const predicted = y0[j] + dt * (f0Arr[j] + f1Arr[j]) / 2;
                const inconsistency = Math.abs(predicted - y1[j]);
                if (inconsistency > maxInconsistency) {
                    maxInconsistency = inconsistency;
                }
            }
        }

        return maxInconsistency * 0.1;
    }

    analyzeConvergence(errors, t) {
        if (!Array.isArray(errors) || errors.length < 2) {
            return { isConvergent: true, order: 0, rate: 0 };
        }

        const logErrors = errors.map(e => Math.log(Math.max(e, 1e-20)));
        const logT = t.map(t => Math.log(Math.max(t, 1e-20)));

        let sumX = 0, sumY = 0, sumXY = 0, sumX2 = 0;
        const n = logErrors.length;

        for (let i = 0; i < n; i++) {
            sumX += logT[i];
            sumY += logErrors[i];
            sumXY += logT[i] * logErrors[i];
            sumX2 += logT[i] * logT[i];
        }

        const denominator = n * sumX2 - sumX * sumX;
        const slope = denominator !== 0 ? (n * sumXY - sumX * sumY) / denominator : 0;

        const lastErrors = errors.slice(-5);
        const isConvergent = lastErrors.every((e, i) => i === 0 || e <= lastErrors[i - 1] * 1.1);

        return {
            isConvergent: isConvergent,
            order: Math.max(0, -slope),
            rate: Math.exp(-slope)
        };
    }

    analyzeStepSizeEffect(stepSize, method) {
        const optimalSteps = {
            'euler': { max: 0.01, recommended: 0.001 },
            'heun': { max: 0.1, recommended: 0.01 },
            'improvedEuler': { max: 0.1, recommended: 0.01 },
            'midpoint': { max: 0.1, recommended: 0.01 },
            'rk2': { max: 0.1, recommended: 0.01 },
            'rk3': { max: 0.2, recommended: 0.05 },
            'rk4': { max: 0.5, recommended: 0.1 },
            'rk5': { max: 1.0, recommended: 0.2 },
            'rk45': { max: 2.0, recommended: 0.1 }
        };

        const optimal = optimalSteps[method] || { max: 0.1, recommended: 0.01 };

        let status = 'good';
        if (stepSize > optimal.max) {
            status = 'too_large';
        } else if (stepSize < optimal.recommended * 0.1) {
            status = 'too_small';
        }

        return {
            currentStepSize: stepSize,
            optimalRange: optimal,
            status: status,
            recommendation: status === 'too_large' ?
                `步长过大，建议减小到 ${optimal.recommended}` :
                status === 'too_small' ?
                    `步长过小，可以增大到 ${optimal.recommended} 提高效率` :
                    '步长合适'
        };
    }

    generateRecommendations(analysis) {
        const recommendations = [];

        const sources = analysis.errorSources;

        if (sources.truncationError.percentage > 50) {
            recommendations.push({
                type: 'truncation',
                severity: 'high',
                message: '截断误差占主导，建议使用更高阶方法或减小步长',
                suggestion: '考虑将方法升级到更高阶，或减小步长以降低截断误差'
            });
        }

        if (sources.roundingError.percentage > 30) {
            recommendations.push({
                type: 'rounding',
                severity: 'medium',
                message: '舍入误差较显著，可能是步长过小导致',
                suggestion: '适当增大步长可以减少计算步骤，降低舍入误差累积'
            });
        }

        if (sources.stabilityError.value > 1e-10) {
            recommendations.push({
                type: 'stability',
                severity: 'high',
                message: '检测到不稳定性，误差可能持续增长',
                suggestion: '检查步长是否在方法的稳定域内，或使用更稳定的方法'
            });
        }

        if (analysis.errorConvergence && !analysis.errorConvergence.isConvergent) {
            recommendations.push({
                type: 'convergence',
                severity: 'critical',
                message: '解可能不收敛',
                suggestion: '尝试减小步长或使用更稳定的方法'
            });
        }

        if (analysis.stepSizeEffect && analysis.stepSizeEffect.status === 'too_large') {
            recommendations.push({
                type: 'stepsize',
                severity: 'medium',
                message: analysis.stepSizeEffect.recommendation,
                suggestion: '减小步长将提高精度'
            });
        }

        if (analysis.reliability === 'low') {
            recommendations.push({
                type: 'reliability',
                severity: 'critical',
                message: '计算结果可靠性低',
                suggestion: '强烈建议重新计算，使用更小步长或更高阶方法'
            });
        }

        return recommendations;
    }

    generateReport(analysis) {
        const lines = [];
        lines.push('=== 误差溯源分析报告 ===');
        lines.push('');
        lines.push(`总误差: ${analysis.totalError.toExponential(6)}`);
        lines.push(`可靠性: ${analysis.reliability}`);
        lines.push('');
        lines.push('--- 误差来源分布 ---');

        for (const [key, source] of Object.entries(analysis.errorSources)) {
            lines.push(`${source.description}: ${source.value.toExponential(6)} (${source.percentage.toFixed(1)}%)`);
        }

        if (analysis.errorConvergence) {
            lines.push('');
            lines.push('--- 收敛性分析 ---');
            lines.push(`收敛阶估计: ${analysis.errorConvergence.order.toFixed(2)}`);
            lines.push(`收敛性: ${analysis.errorConvergence.isConvergent ? '是' : '否'}`);
        }

        if (analysis.stepSizeEffect) {
            lines.push('');
            lines.push('--- 步长影响分析 ---');
            lines.push(`当前步长: ${analysis.stepSizeEffect.currentStepSize}`);
            lines.push(`步长状态: ${analysis.stepSizeEffect.status}`);
            lines.push(`建议: ${analysis.stepSizeEffect.recommendation}`);
        }

        if (analysis.recommendations.length > 0) {
            lines.push('');
            lines.push('--- 优化建议 ---');
            for (const rec of analysis.recommendations) {
                lines.push(`[${rec.severity.toUpperCase()}] ${rec.message}`);
                lines.push(`  建议: ${rec.suggestion}`);
            }
        }

        return lines.join('\n');
    }
}

if (typeof module !== 'undefined' && module.exports) {
    module.exports = ErrorSourceAnalyzer;
}
