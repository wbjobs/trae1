class EulerSolver {
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

    euler(f, t0, y0, tEnd, h = this.stepSize) {
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
                const dydt = f(t, y);

                if (!this.isFiniteArray(dydt)) {
                    throw new Error('导数计算产生非有限值');
                }

                const yNew = y.map((yi, i) => yi + step * dydt[i]);

                if (!this.isFiniteArray(yNew)) {
                    if (step > this.minStepSize) {
                        step = Math.max(step / 2, this.minStepSize);
                        consecutiveErrors++;
                        if (consecutiveErrors > 20) {
                            throw new Error('数值发散：无法收敛到有限值');
                        }
                        continue;
                    }
                    throw new Error('数值溢出：结果超出范围');
                }

                if (this.checkOverflow(yNew)) {
                    throw new Error('数值溢出：结果值过大');
                }

                if (this.adaptiveStep) {
                    const stepResult = this.adaptiveStepEuler(f, t, y, step);
                    t = stepResult.t;
                    y = stepResult.y;
                    step = stepResult.h;
                } else {
                    t += step;
                    y = yNew;
                }

                consecutiveErrors = 0;
                results.push({ t, y: [...y] });

            } catch (error) {
                if (error.message.includes('数值') && step > this.minStepSize) {
                    step = Math.max(step / 2, this.minStepSize);
                    consecutiveErrors++;
                    if (consecutiveErrors > 20) {
                        throw error;
                    }
                } else {
                    throw error;
                }
            }
        }

        return results;
    }

    eulerImproved(f, t0, y0, tEnd, h = this.stepSize) {
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
                    const stepResult = this.adaptiveStepImproved(f, t, y, step);

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
                    const k1 = f(t, y);
                    const yPredict = y.map((yi, i) => yi + step * k1[i]);

                    if (!this.isFiniteArray(yPredict)) {
                        if (step > this.minStepSize) {
                            step = Math.max(step / 2, this.minStepSize);
                            continue;
                        }
                        throw new Error('预测步发散');
                    }

                    const k2 = f(t + step, yPredict);
                    y = y.map((yi, i) => yi + step * (k1[i] + k2[i]) / 2);
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
                    if (consecutiveErrors > 20) {
                        throw error;
                    }
                } else {
                    throw error;
                }
            }
        }

        return results;
    }

    midpointMethod(f, t0, y0, tEnd, h = this.stepSize) {
        const results = [{ t: t0, y: [...y0] }];
        let t = t0;
        let y = [...y0];
        let step = h;

        while (t < tEnd) {
            if (t + step > tEnd) step = tEnd - t;

            const k1 = f(t, y);
            const yMid = y.map((yi, i) => yi + step * k1[i] / 2);
            const k2 = f(t + step / 2, yMid);
            y = y.map((yi, i) => yi + step * k2[i]);
            t += step;

            if (!this.isFiniteArray(y)) {
                throw new Error('中点法产生非有限值');
            }

            results.push({ t, y: [...y] });
        }

        return results;
    }

    heunMethod(f, t0, y0, tEnd, h = this.stepSize) {
        const results = [{ t: t0, y: [...y0] }];
        let t = t0;
        let y = [...y0];
        let step = h;

        while (t < tEnd) {
            if (t + step > tEnd) step = tEnd - t;

            const k1 = f(t, y);
            const yPredict = y.map((yi, i) => yi + step * k1[i]);
            const k2 = f(t + step, yPredict);

            let yNew = y.map((yi, i) => yi + step * (k1[i] + k2[i]) / 2);
            let k3 = f(t + step, yNew);

            yNew = y.map((yi, i) => yi + step * (k1[i] + 2 * k2[i] + k3[i]) / 4);

            y = yNew;
            t += step;

            if (!this.isFiniteArray(y)) {
                throw new Error('Heun法产生非有限值');
            }

            results.push({ t, y: [...y] });
        }

        return results;
    }

    adaptiveStepEuler(f, t, y, h) {
        let step = h;
        const maxIterations = 10;

        for (let iter = 0; iter < maxIterations; iter++) {
            const halfStep = step / 2;

            const dydt = f(t, y);
            const yFull = y.map((yi, i) => yi + step * dydt[i]);

            if (!this.isFiniteArray(yFull)) {
                step = Math.max(step / 2, this.minStepSize);
                continue;
            }

            const yHalf = y.map((yi, i) => yi + halfStep * dydt[i]);
            const dydtHalf = f(t + halfStep, yHalf);
            const yDoubleHalf = yHalf.map((yi, i) => yi + halfStep * dydtHalf[i]);

            if (!this.isFiniteArray(yDoubleHalf)) {
                step = Math.max(step / 2, this.minStepSize);
                continue;
            }

            const error = this.calculateError(yFull, yDoubleHalf);

            if (!this.isFinite(error) || error < 0) {
                step = Math.max(step / 2, this.minStepSize);
                continue;
            }

            if (error <= this.tolerance || step <= this.minStepSize) {
                return {
                    t: t + step,
                    y: yDoubleHalf,
                    h: this.adjustStepSize(step, error, 1)
                };
            }

            step = Math.max(step * 0.5, this.minStepSize);
        }

        const dydt = f(t, y);
        const yResult = y.map((yi, i) => yi + step * dydt[i]);

        if (!this.isFiniteArray(yResult)) {
            throw new Error('自适应欧拉法：无法获得有限结果');
        }

        return {
            t: t + step,
            y: yResult,
            h: step
        };
    }

    adaptiveStepImproved(f, t, y, h) {
        let step = h;
        const maxIterations = 10;

        for (let iter = 0; iter < maxIterations; iter++) {
            const halfStep = step / 2;

            let yFull, yHalf2;

            try {
                yFull = this.improvedStep(f, t, y, step);
                const yHalf1 = this.improvedStep(f, t, y, halfStep);
                yHalf2 = this.improvedStep(f, t + halfStep, yHalf1, halfStep);
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
                const yNew = this.safeRichardsonExtrapolation(yFull, yHalf2, 2);
                return {
                    t: t + step,
                    y: this.isFiniteArray(yNew) ? yNew : yHalf2,
                    h: this.adjustStepSize(step, error, 2)
                };
            }

            step = Math.max(step * 0.5, this.minStepSize);
        }

        return {
            t: t + step,
            y: this.improvedStep(f, t, y, step),
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

    improvedStep(f, t, y, h) {
        const k1 = f(t, y);
        const yPredict = y.map((yi, i) => yi + h * k1[i]);

        if (!this.isFiniteArray(yPredict)) {
            throw new Error('预测步发散');
        }

        const k2 = f(t + h, yPredict);
        return y.map((yi, i) => yi + h * (k1[i] + k2[i]) / 2);
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

    adjustStepSize(h, error, order = 1) {
        if (!this.isFinite(error) || error === 0) {
            return Math.min(h * this.maxScale, this.maxStepSize);
        }

        const exponent = 1 / (order + 1);
        const ratio = this.tolerance / error;

        if (!this.isFinite(ratio)) {
            return Math.max(this.minStepSize, h / 2);
        }

        let scale = this.safetyFactor * Math.pow(Math.min(ratio, 1e10), exponent);

        if (!this.isFinite(scale)) {
            return Math.max(this.minStepSize, h / 2);
        }

        scale = Math.max(this.minScale, Math.min(scale, this.maxScale));
        return Math.max(this.minStepSize, Math.min(h * scale, this.maxStepSize));
    }

    solve(equation, initialConditions, tStart, tEnd, method = 'euler') {
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
            case 'euler':
                return this.euler(f, tStart, y0, tEnd);
            case 'euler_improved':
                return this.eulerImproved(f, tStart, y0, tEnd);
            case 'midpoint':
                return this.midpointMethod(f, tStart, y0, tEnd);
            case 'heun':
                return this.heunMethod(f, tStart, y0, tEnd);
            default:
                return this.eulerImproved(f, tStart, y0, tEnd);
        }
    }
}

if (typeof module !== 'undefined' && module.exports) {
    module.exports = EulerSolver;
}
