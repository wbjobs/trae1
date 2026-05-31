class RungeKuttaSolver {
    constructor(config = {}) {
        this.stepSize = config.stepSize || 0.01;
        this.tolerance = config.tolerance || 1e-8;
        this.adaptiveStep = config.adaptiveStep !== false;
        this.safetyFactor = config.safetyFactor || 0.9;
        this.minScale = config.minScale || 0.2;
        this.maxScale = config.maxScale || 5.0;
        this.minStepSize = config.minStepSize || 1e-10;
        this.maxStepSize = config.maxStepSize || 0.1;
        this.maxValue = config.maxValue || 1e10;
        this.stiffnessThreshold = config.stiffnessThreshold || 1000;

        this.usePIController = config.usePIController !== false;
        this.piBeta = config.piBeta || 0.08;
        this.maxRetry = config.maxRetry || 20;
        this.errorHistory = [];
        this.lastError = null;
    }

    isFiniteArray(arr) {
        return arr.every(v => isFinite(v));
    }

    isFinite(v) {
        return typeof v === 'number' && !isNaN(v) && isFinite(v);
    }

    checkOverflow(y) {
        return y.some(v => Math.abs(v) > this.maxValue);
    }

    estimateStiffness(f, t, y, h) {
        const dy = f(t, y);
        let maxRatio = 0;

        for (let i = 0; i < y.length; i++) {
            const yScale = Math.max(Math.abs(y[i]), 1e-10);
            const dyScale = Math.max(Math.abs(dy[i]), 1e-10);
            const ratio = (dyScale / yScale) * h;
            maxRatio = Math.max(maxRatio, ratio);
        }

        return maxRatio;
    }

    safeStep(f, t, y, h, stepFn) {
        const result = stepFn(f, t, y, h);

        if (!this.isFiniteArray(result)) {
            throw new Error('步骤产生非有限值');
        }

        if (this.checkOverflow(result)) {
            throw new Error('数值溢出：结果值过大');
        }

        return result;
    }

    rk2(f, t0, y0, tEnd, h = this.stepSize) {
        return this.solveWithMethod(f, t0, y0, tEnd, h, this.rk2Step.bind(this), 2);
    }

    rk2Step(f, t, y, h) {
        const k1 = f(t, y);
        const yHalf = y.map((yi, i) => yi + h * k1[i] / 2);
        const k2 = f(t + h / 2, yHalf);
        return y.map((yi, i) => yi + h * k2[i]);
    }

    rk3(f, t0, y0, tEnd, h = this.stepSize) {
        return this.solveWithMethod(f, t0, y0, tEnd, h, this.rk3Step.bind(this), 3);
    }

    rk3Step(f, t, y, h) {
        const k1 = f(t, y);
        const y2 = y.map((yi, i) => yi + h * k1[i] / 2);
        const k2 = f(t + h / 2, y2);
        const y3 = y.map((yi, i) => yi - h * k1[i] + 2 * h * k2[i]);
        const k3 = f(t + h, y3);
        return y.map((yi, i) => yi + h * (k1[i] + 4 * k2[i] + k3[i]) / 6);
    }

    rk4(f, t0, y0, tEnd, h = this.stepSize) {
        return this.solveWithMethod(f, t0, y0, tEnd, h, this.rk4Step.bind(this), 4);
    }

    rk4Step(f, t, y, h) {
        const k1 = f(t, y);

        const y2 = y.map((yi, i) => yi + h * k1[i] / 2);
        const k2 = f(t + h / 2, y2);

        const y3 = y.map((yi, i) => yi + h * k2[i] / 2);
        const k3 = f(t + h / 2, y3);

        const y4 = y.map((yi, i) => yi + h * k3[i]);
        const k4 = f(t + h, y4);

        return y.map((yi, i) => yi + h * (k1[i] + 2 * k2[i] + 2 * k3[i] + k4[i]) / 6);
    }

    solveWithMethod(f, t0, y0, tEnd, h, stepFn, order) {
        const results = [{ t: t0, y: [...y0] }];
        let t = t0;
        let y = [...y0];
        let step = h;
        let consecutiveErrors = 0;

        while (t < tEnd) {
            if (t + step > tEnd) {
                step = tEnd - t;
            }

            const stiffness = this.estimateStiffness(f, t, y, step);
            if (stiffness > this.stiffnessThreshold) {
                step = Math.max(step / 2, this.minStepSize);
            }

            try {
                if (this.adaptiveStep) {
                    const stepResult = this.adaptiveStepGeneric(f, t, y, step, stepFn, order);

                    if (!this.isFiniteArray(stepResult.y)) {
                        if (step > this.minStepSize) {
                            step = Math.max(step / 2, this.minStepSize);
                            consecutiveErrors++;
                            if (consecutiveErrors > 20) {
                                throw new Error('数值发散：自适应步长无法收敛');
                            }
                            continue;
                        }
                        throw new Error('数值溢出');
                    }

                    t = stepResult.t;
                    y = stepResult.y;
                    step = stepResult.h;
                } else {
                    y = this.safeStep(f, t, y, step, stepFn);
                    t += step;
                }

                if (!this.isFiniteArray(y)) {
                    throw new Error('结果包含非有限值');
                }

                if (this.checkOverflow(y)) {
                    throw new Error('数值溢出：结果值过大');
                }

                consecutiveErrors = 0;
                results.push({ t, y: [...y] });

            } catch (error) {
                if (step > this.minStepSize) {
                    step = Math.max(step / 2, this.minStepSize);
                    consecutiveErrors++;
                    if (consecutiveErrors > 50) {
                        throw error;
                    }
                } else {
                    throw error;
                }
            }
        }

        return results;
    }

    rk45(f, t0, y0, tEnd, h = this.stepSize) {
        const results = [{ t: t0, y: [...y0] }];
        let t = t0;
        let y = [...y0];
        let step = h;
        let consecutiveErrors = 0;

        while (t < tEnd) {
            if (t + step > tEnd) {
                step = tEnd - t;
            }

            const stiffness = this.estimateStiffness(f, t, y, step);
            if (stiffness > this.stiffnessThreshold) {
                step = Math.max(step / 2, this.minStepSize);
            }

            try {
                const stepResult = this.rk45Step(f, t, y, step);
                const error = stepResult.error;

                if (!this.isFinite(error) || error < 0) {
                    if (step > this.minStepSize) {
                        step = Math.max(step / 2, this.minStepSize);
                        consecutiveErrors++;
                        continue;
                    }
                    throw new Error('误差估计失败');
                }

                if (error <= this.tolerance || step <= this.minStepSize) {
                    if (!this.isFiniteArray(stepResult.y5)) {
                        if (step > this.minStepSize) {
                            step = Math.max(step / 2, this.minStepSize);
                            continue;
                        }
                        throw new Error('RK45产生非有限值');
                    }

                    t += step;
                    y = stepResult.y5;

                    if (this.checkOverflow(y)) {
                        throw new Error('数值溢出：结果值过大');
                    }

                    results.push({ t, y: [...y] });
                    consecutiveErrors = 0;
                }

                const newStep = this.adjustStepSize(step, error, 5);
                step = Math.max(this.minStepSize, Math.min(newStep, this.maxStepSize));

            } catch (error) {
                if (step > this.minStepSize) {
                    step = Math.max(step / 2, this.minStepSize);
                    consecutiveErrors++;
                    if (consecutiveErrors > 50) {
                        throw error;
                    }
                } else {
                    throw error;
                }
            }
        }

        return results;
    }

    rk45Step(f, t, y, h) {
        const c = [0, 1/4, 3/8, 12/13, 1, 1/2];
        const a = [
            [],
            [1/4],
            [3/32, 9/32],
            [1932/2197, -7200/2197, 7296/2197],
            [439/216, -8, 3680/513, -845/4104],
            [-8/27, 2, -3544/2565, 1859/4104, -11/40]
        ];
        const b4 = [25/216, 0, 1408/2565, 2197/4104, -1/5, 0];
        const b5 = [16/135, 0, 6656/12825, 28561/56430, -9/50, 2/55];

        const k = [];
        for (let i = 0; i < 6; i++) {
            let yi = [...y];
            for (let j = 0; j < i; j++) {
                yi = yi.map((v, idx) => v + h * a[i][j] * k[j][idx]);
            }

            if (!this.isFiniteArray(yi)) {
                throw new Error('RK45中间步骤发散');
            }

            k.push(f(t + c[i] * h, yi));

            if (!this.isFiniteArray(k[k.length - 1])) {
                throw new Error('RK45导数计算失败');
            }
        }

        const y4 = [...y];
        const y5 = [...y];

        for (let i = 0; i < y.length; i++) {
            let sum4 = 0, sum5 = 0;
            for (let j = 0; j < 6; j++) {
                if (this.isFinite(k[j][i])) {
                    sum4 += b4[j] * k[j][i];
                    sum5 += b5[j] * k[j][i];
                }
            }
            y4[i] = y[i] + h * sum4;
            y5[i] = y[i] + h * sum5;
        }

        const error = this.calculateError(y4, y5);

        return { y4, y5, error };
    }

    rkf45(f, t0, y0, tEnd, h = this.stepSize) {
        return this.rk45(f, t0, y0, tEnd, h);
    }

    rk5(f, t0, y0, tEnd, h = this.stepSize) {
        const results = [{ t: t0, y: [...y0] }];
        let t = t0;
        let y = [...y0];
        let step = h;

        while (t < tEnd) {
            if (t + step > tEnd) {
                step = tEnd - t;
            }

            y = this.rk5Step(f, t, y, step);
            t += step;

            if (!this.isFiniteArray(y)) {
                throw new Error('RK5产生非有限值');
            }

            if (this.checkOverflow(y)) {
                throw new Error('RK5数值溢出');
            }

            results.push({ t, y: [...y] });
        }

        return results;
    }

    rk5Step(f, t, y, h) {
        const c = [0, 1/6, 1/3, 1/2, 2/3, 1];
        const a = [
            [],
            [1/6],
            [1/12, 1/4],
            [0, 1/2, 0],
            [2/3, -1/3, 0, 2/3],
            [1/8, 3/8, 0, 3/8, 1/8]
        ];
        const b = [7/90, 0, 16/45, 2/15, 16/45, 7/90];

        const k = [];
        for (let i = 0; i < 6; i++) {
            let yi = [...y];
            for (let j = 0; j < i; j++) {
                yi = yi.map((v, idx) => v + h * a[i][j] * k[j][idx]);
            }
            k.push(f(t + c[i] * h, yi));
        }

        const result = [...y];
        for (let j = 0; j < 6; j++) {
            for (let i = 0; i < y.length; i++) {
                result[i] += h * b[j] * k[j][i];
            }
        }

        return result;
    }

    adaptiveStepGeneric(f, t, y, h, stepFn, order) {
        let step = h;
        const maxIterations = 10;

        for (let iter = 0; iter < maxIterations; iter++) {
            const halfStep = step / 2;

            let yFull, yHalf2;

            try {
                yFull = stepFn(f, t, y, step);
                const yHalf1 = stepFn(f, t, y, halfStep);
                yHalf2 = stepFn(f, t + halfStep, yHalf1, halfStep);
            } catch (e) {
                step = Math.max(step / 2, this.minStepSize);
                continue;
            }

            if (!this.isFiniteArray(yFull) || !this.isFiniteArray(yHalf2)) {
                step = Math.max(step / 2, this.minStepSize);
                continue;
            }

            const error = this.calculateError(yFull, yHalf2);

            if (!this.isFinite(error) || error < 0) {
                step = Math.max(step / 2, this.minStepSize);
                continue;
            }

            if (error <= this.tolerance || step <= this.minStepSize) {
                const yNew = this.safeRichardsonExtrapolation(yFull, yHalf2, order);
                return {
                    t: t + step,
                    y: this.isFiniteArray(yNew) ? yNew : yHalf2,
                    h: this.adjustStepSize(step, error, order)
                };
            }

            step = Math.max(step * 0.5, this.minStepSize);
        }

        const result = stepFn(f, t, y, step);

        if (!this.isFiniteArray(result)) {
            throw new Error('自适应步长失败：无法获得有限结果');
        }

        return {
            t: t + step,
            y: result,
            h: step
        };
    }

    safeRichardsonExtrapolation(yCoarse, yFine, order) {
        const factor = Math.pow(2, order);
        const result = [];

        for (let i = 0; i < yCoarse.length; i++) {
            const extrapolated = (factor * yFine[i] - yCoarse[i]) / (factor - 1);

            if (!this.isFinite(extrapolated) || Math.abs(extrapolated) > this.maxValue) {
                result.push(yFine[i]);
            } else {
                const diff = Math.abs(extrapolated - yFine[i]);
                const scale = Math.max(Math.abs(yFine[i]), 1e-10);
                if (diff / scale > 10) {
                    result.push(yFine[i]);
                } else {
                    result.push(extrapolated);
                }
            }
        }

        return result;
    }

    calculateError(y1, y2) {
        let maxError = 0;
        for (let i = 0; i < y1.length; i++) {
            const error = Math.abs(y1[i] - y2[i]);
            const scale = Math.max(Math.abs(y1[i]), Math.abs(y2[i]), 1e-10);
            const relativeError = error / scale;
            if (this.isFinite(relativeError)) {
                maxError = Math.max(maxError, relativeError);
            }
        }
        return maxError;
    }

    adjustStepSize(h, error, order) {
        if (!this.isFinite(error) || error === 0) {
            return Math.min(h * this.maxScale, this.maxStepSize);
        }

        const exponent = 1 / (order + 1);
        const ratio = this.tolerance / error;

        if (!this.isFinite(ratio)) {
            return Math.max(this.minStepSize, h / 2);
        }

        let scale;

        if (this.usePIController && this.lastError !== null) {
            const piExponent = 1 / (order + 1);
            const piController = Math.pow(ratio, piExponent) *
                Math.pow(error / this.lastError, -this.piBeta * exponent);
            scale = this.safetyFactor * piController;
        } else {
            scale = this.safetyFactor * Math.pow(Math.min(ratio, 1e10), exponent);
        }

        if (!this.isFinite(scale)) {
            return Math.max(this.minStepSize, h / 2);
        }

        scale = Math.max(this.minScale, Math.min(scale, this.maxScale));

        this.lastError = error;
        this.errorHistory.push(error);
        if (this.errorHistory.length > 10) {
            this.errorHistory.shift();
        }

        return Math.max(this.minStepSize, Math.min(h * scale, this.maxStepSize));
    }

    getAdaptiveStatistics() {
        if (this.errorHistory.length === 0) {
            return { avgError: 0, minError: 0, maxError: 0, steps: 0 };
        }

        const avgError = this.errorHistory.reduce((a, b) => a + b, 0) / this.errorHistory.length;
        const minError = Math.min(...this.errorHistory);
        const maxError = Math.max(...this.errorHistory);

        return {
            avgError: avgError,
            minError: minError,
            maxError: maxError,
            steps: this.errorHistory.length
        };
    }

    checkConvergence() {
        if (this.errorHistory.length < 5) return { converged: true, trend: 'insufficient_data' };

        const recent = this.errorHistory.slice(-5);
        const earlier = this.errorHistory.slice(-10, -5);

        const recentAvg = recent.reduce((a, b) => a + b, 0) / recent.length;

        if (earlier.length === 0) {
            return { converged: true, trend: 'stable', recentAvg: recentAvg };
        }

        const earlierAvg = earlier.reduce((a, b) => a + b, 0) / earlier.length;

        let trend = 'stable';
        if (recentAvg > earlierAvg * 1.5) {
            trend = 'increasing';
        } else if (recentAvg < earlierAvg * 0.5) {
            trend = 'decreasing';
        }

        return {
            converged: recentAvg <= this.tolerance * 10,
            trend: trend,
            recentAvg: recentAvg,
            earlierAvg: earlierAvg
        };
    }

    solve(equation, initialConditions, tStart, tEnd, method = 'rk4') {
        const f = (t, y) => {
            if (equation.type === 'system') {
                return equation.evaluate(t, y);
            }
            const result = equation.evaluate(t, y[0]);
            return [result];
        };

        const y0 = Array.isArray(initialConditions) ? initialConditions : [initialConditions];

        if (!this.isFiniteArray(y0)) {
            throw new Error('初始条件包含非有限值');
        }

        switch (method) {
            case 'rk2':
                return this.rk2(f, tStart, y0, tEnd);
            case 'rk3':
                return this.rk3(f, tStart, y0, tEnd);
            case 'rk4':
                return this.rk4(f, tStart, y0, tEnd);
            case 'rk5':
                return this.rk5(f, tStart, y0, tEnd);
            case 'rk45':
            case 'rkf45':
                return this.rk45(f, tStart, y0, tEnd);
            default:
                return this.rk4(f, tStart, y0, tEnd);
        }
    }
}

if (typeof module !== 'undefined' && module.exports) {
    module.exports = RungeKuttaSolver;
}
