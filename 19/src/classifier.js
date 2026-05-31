class EquationClassifier {
    constructor(config = {}) {
        this.stiffnessThreshold = config.stiffnessThreshold || 10;
        this.linearityTolerance = config.linearityTolerance || 1e-10;
        this.maxSamplePoints = config.maxSamplePoints || 5;
    }

    classify(f, t0, y0, tEnd) {
        const yArray = Array.isArray(y0) ? y0 : [y0];
        const dimension = yArray.length;

        const analysis = {
            dimension: dimension,
            type: 'general',
            isLinear: false,
            isAutonomous: false,
            isStiff: false,
            stiffnessRatio: 0,
            isExponential: false,
            isOscillatory: false,
            isSmooth: true,
            hasSingularity: false,
            recommendedMethods: [],
            analysisDetails: {}
        };

        analysis.isAutonomous = this.checkAutonomy(f, t0, yArray);

        analysis.isLinear = this.checkLinearity(f, t0, yArray);

        const stiffnessInfo = this.estimateStiffness(f, t0, yArray, tEnd - t0);
        analysis.isStiff = stiffnessInfo.isStiff;
        analysis.stiffnessRatio = stiffnessInfo.ratio;
        analysis.stiffnessEigenvalues = stiffnessInfo.eigenvalues;

        analysis.isExponential = this.checkExponential(f, t0, yArray);

        analysis.isOscillatory = this.checkOscillatory(f, t0, yArray);

        analysis.isSmooth = this.checkSmoothness(f, t0, yArray);

        analysis.hasSingularity = this.checkSingularity(f, t0, yArray, tEnd);

        if (analysis.isStiff) {
            analysis.type = 'stiff';
        } else if (analysis.isOscillatory) {
            analysis.type = 'oscillatory';
        } else if (analysis.isExponential) {
            analysis.type = 'exponential';
        } else if (analysis.isLinear) {
            analysis.type = 'linear';
        }

        analysis.recommendedMethods = this.recommendMethods(analysis);
        analysis.analysisDetails = this.generateDetails(analysis);

        return analysis;
    }

    checkAutonomy(f, t0, y) {
        try {
            const d1 = f(t0, [...y]);
            const d2 = f(t0 + 1, [...y]);

            if (Array.isArray(d1) && Array.isArray(d2)) {
                for (let i = 0; i < d1.length; i++) {
                    if (Math.abs(d1[i] - d2[i]) > 1e-12) return false;
                }
                return true;
            }
            return Math.abs(d1 - d2) < 1e-12;
        } catch (e) {
            return false;
        }
    }

    checkLinearity(f, t0, y) {
        try {
            const n = y.length;
            const f0 = f(t0, y);

            const scale = 2.0;
            const yScaled = y.map(v => v * scale);
            const fScaled = f(t0, yScaled);

            for (let j = 0; j < f0.length; j++) {
                const expectedScaled = scale * f0[j];
                const actualScaled = fScaled[j];
                if (Math.abs(expectedScaled - actualScaled) > this.linearityTolerance * Math.max(1, Math.abs(expectedScaled))) {
                    return false;
                }
            }

            const halfScale = 0.5;
            const yHalf = y.map(v => v * halfScale);
            const fHalf = f(t0, yHalf);

            for (let j = 0; j < f0.length; j++) {
                const expectedHalf = halfScale * f0[j];
                const actualHalf = fHalf[j];
                if (Math.abs(expectedHalf - actualHalf) > this.linearityTolerance * Math.max(1, Math.abs(expectedHalf))) {
                    return false;
                }
            }

            const yAdd = y.map((v, i) => v + yHalf[i]);
            const fAdd = f(t0, yAdd);
            const expectedAdd = f0.map((v, i) => v + fHalf[i]);

            for (let j = 0; j < f0.length; j++) {
                if (Math.abs(expectedAdd[j] - fAdd[j]) > this.linearityTolerance * Math.max(1, Math.abs(expectedAdd[j]))) {
                    return false;
                }
            }

            return true;
        } catch (e) {
            return false;
        }
    }

    estimateStiffness(f, t0, y, totalTime) {
        const n = y.length;
        const h = Math.min(totalTime * 0.01, 0.001);

        const f0 = f(t0, y);

        const jacobian = [];
        for (let i = 0; i < n; i++) {
            jacobian.push(new Array(n).fill(0));
        }

        for (let i = 0; i < n; i++) {
            const yPerturbed = [...y];
            const perturbation = Math.max(Math.abs(y[i]) * 1e-6, 1e-8);
            yPerturbed[i] += perturbation;

            const fPerturbed = f(t0, yPerturbed);

            for (let j = 0; j < n; j++) {
                jacobian[j][i] = (fPerturbed[j] - f0[j]) / perturbation;
            }
        }

        const eigenvalues = this.estimateEigenvalues(jacobian);

        let maxEigenvalue = 0;
        let minEigenvalue = Infinity;

        for (const eig of eigenvalues) {
            const absEig = Math.abs(eig);
            if (absEig > maxEigenvalue) maxEigenvalue = absEig;
            if (absEig < minEigenvalue && absEig > 1e-10) minEigenvalue = absEig;
        }

        const ratio = minEigenvalue > 0 ? maxEigenvalue / minEigenvalue : 0;

        const stiffnessProduct = maxEigenvalue * h;

        return {
            isStiff: ratio > this.stiffnessThreshold || stiffnessProduct > 1,
            ratio: ratio,
            stiffnessProduct: stiffnessProduct,
            eigenvalues: eigenvalues,
            maxEigenvalue: maxEigenvalue,
            minEigenvalue: minEigenvalue
        };
    }

    estimateEigenvalues(jacobian) {
        const n = jacobian.length;

        if (n === 1) {
            return [jacobian[0][0]];
        }

        let trace = 0;
        let det = 0;

        if (n === 2) {
            trace = jacobian[0][0] + jacobian[1][1];
            det = jacobian[0][0] * jacobian[1][1] - jacobian[0][1] * jacobian[1][0];

            const discriminant = trace * trace - 4 * det;
            if (discriminant >= 0) {
                const sqrtDisc = Math.sqrt(discriminant);
                return [(trace + sqrtDisc) / 2, (trace - sqrtDisc) / 2];
            } else {
                return [Math.abs(trace) / 2, Math.abs(trace) / 2];
            }
        }

        const rowSums = [];
        for (let i = 0; i < n; i++) {
            let sum = 0;
            for (let j = 0; j < n; j++) {
                sum += Math.abs(jacobian[i][j]);
            }
            rowSums.push(sum);
        }

        const maxRowSum = Math.max(...rowSums);
        const minRowSum = Math.min(...rowSums.filter(s => s > 1e-10));

        return [maxRowSum, minRowSum || maxRowSum];
    }

    checkExponential(f, t0, y) {
        try {
            const f0 = f(t0, y);

            for (let i = 0; i < y.length; i++) {
                const yScale = Math.max(Math.abs(y[i]), 1e-10);
                const fScale = Math.abs(f0[i]);

                if (fScale / yScale > 1e-10) {
                    const y2 = [...y];
                    y2[i] = y[i] * 1.5;
                    const f2 = f(t0, y2);

                    const ratio1 = f0[i] / y[i];
                    const ratio2 = f2[i] / y2[i];

                    if (Math.abs(ratio1 - ratio2) / Math.abs(ratio1) < 0.01) {
                        return true;
                    }
                }
            }
            return false;
        } catch (e) {
            return false;
        }
    }

    checkOscillatory(f, t0, y) {
        try {
            if (y.length < 2) return false;

            const f0 = f(t0, y);

            for (let i = 0; i < y.length - 1; i++) {
                const dydt_i = f0[i];
                const dydt_j = f0[i + 1];

                if (Math.abs(dydt_i) > 1e-10 && Math.abs(dydt_j) > 1e-10) {
                    const yPerturbed = [...y];
                    yPerturbed[i + 1] += 0.01;
                    const fPerturbed = f(t0, yPerturbed);

                    if (Math.sign(dydt_i) !== Math.sign(fPerturbed[i])) {
                        return true;
                    }
                }
            }

            if (y.length >= 2) {
                const f1 = f(t0, y);
                const negY = y.map((val, i) => i < 2 ? -val : val);
                const f2 = f(t0, negY);

                let hasOppositeSign = false;
                for (let i = 0; i < Math.min(2, f1.length); i++) {
                    if (Math.sign(f1[i]) === -Math.sign(f2[i]) && Math.abs(f1[i]) > 1e-10) {
                        hasOppositeSign = true;
                        break;
                    }
                }
                if (hasOppositeSign) return true;
            }

            return false;
        } catch (e) {
            return false;
        }
    }

    checkSmoothness(f, t0, y) {
        try {
            const h = 1e-6;
            const f1 = f(t0, y);
            const f2 = f(t0 + h, y);

            if (Array.isArray(f1)) {
                for (let i = 0; i < f1.length; i++) {
                    if (!isFinite(f1[i]) || !isFinite(f2[i])) return false;
                }
            } else {
                if (!isFinite(f1) || !isFinite(f2)) return false;
            }

            return true;
        } catch (e) {
            return false;
        }
    }

    checkSingularity(f, t0, y, tEnd) {
        try {
            const testPoints = [t0, t0 + (tEnd - t0) * 0.25, t0 + (tEnd - t0) * 0.5, t0 + (tEnd - t0) * 0.75, tEnd];

            for (const t of testPoints) {
                const result = f(t, y);
                if (Array.isArray(result)) {
                    for (const val of result) {
                        if (!isFinite(val) || Math.abs(val) > 1e100) return true;
                    }
                } else {
                    if (!isFinite(result) || Math.abs(result) > 1e100) return true;
                }
            }

            return false;
        } catch (e) {
            return true;
        }
    }

    recommendMethods(analysis) {
        const methods = [];

        if (analysis.isStiff) {
            methods.push({ method: 'rk45', score: 90, reason: '刚性问题推荐使用自适应步长方法' });
            methods.push({ method: 'rk4', score: 80, reason: 'RK4具有较好的稳定性' });
            methods.push({ method: 'heun', score: 60, reason: 'Heun法比欧拉法稳定' });
            methods.push({ method: 'euler', score: 30, reason: '欧拉法可能不稳定，需要极小步长' });
        } else if (analysis.isOscillatory) {
            methods.push({ method: 'rk4', score: 95, reason: '振荡问题推荐RK4，具有良好的能量守恒特性' });
            methods.push({ method: 'rk45', score: 85, reason: '自适应RK45可有效处理振荡' });
            methods.push({ method: 'midpoint', score: 70, reason: '中点法具有辛特性' });
            methods.push({ method: 'euler', score: 40, reason: '欧拉法存在数值耗散' });
        } else if (analysis.isExponential) {
            methods.push({ method: 'rk4', score: 90, reason: '指数衰减问题RK4精度高' });
            methods.push({ method: 'heun', score: 85, reason: 'Heun法对指数问题效果好' });
            methods.push({ method: 'rk2', score: 75, reason: 'RK2性价比高' });
            methods.push({ method: 'euler', score: 70, reason: '欧拉法简单且在稳定域内有效' });
        } else if (analysis.isLinear) {
            methods.push({ method: 'rk4', score: 90, reason: '线性问题高阶方法精度高' });
            methods.push({ method: 'rk45', score: 85, reason: '自适应方法效率高' });
            methods.push({ method: 'heun', score: 75, reason: 'Heun法性价比高' });
        } else {
            methods.push({ method: 'rk45', score: 95, reason: '一般问题推荐自适应RK45' });
            methods.push({ method: 'rk4', score: 85, reason: 'RK4是经典选择' });
            methods.push({ method: 'rk3', score: 70, reason: 'RK3效率较高' });
            methods.push({ method: 'heun', score: 65, reason: 'Heun法简单有效' });
        }

        return methods.sort((a, b) => b.score - a.score);
    }

    generateDetails(analysis) {
        const details = {
            summary: '',
            warnings: [],
            suggestions: []
        };

        const typeNames = {
            'general': '一般微分方程',
            'stiff': '刚性微分方程',
            'oscillatory': '振荡型微分方程',
            'exponential': '指数型微分方程',
            'linear': '线性微分方程'
        };

        details.summary = `检测到方程类型: ${typeNames[analysis.type] || '未知类型'}`;

        if (analysis.isStiff) {
            details.warnings.push(`方程具有刚性，刚度比约为 ${analysis.stiffnessRatio.toFixed(2)}`);
            details.suggestions.push('使用自适应步长方法，避免显式欧拉法');
        }

        if (analysis.isOscillatory) {
            details.suggestions.push('振荡问题建议使用具有能量守恒特性的方法');
        }

        if (analysis.hasSingularity) {
            details.warnings.push('检测到可能的奇异点，求解可能失败');
            details.suggestions.push('考虑缩小求解区间或使用特殊方法处理奇异性');
        }

        if (!analysis.isSmooth) {
            details.warnings.push('方程右端函数可能不光滑');
            details.suggestions.push('建议使用对不光滑性更鲁棒的低阶方法');
        }

        if (analysis.recommendedMethods.length > 0) {
            const best = analysis.recommendedMethods[0];
            details.suggestions.push(`推荐首选方法: ${best.method} (评分: ${best.score})`);
        }

        return details;
    }
}

if (typeof module !== 'undefined' && module.exports) {
    module.exports = EquationClassifier;
}
