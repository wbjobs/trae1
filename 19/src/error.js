class ErrorController {
    constructor(config = {}) {
        this.tolerance = config.tolerance || 1e-8;
        this.relativeTolerance = config.relativeTolerance || 1e-8;
        this.absoluteTolerance = config.absoluteTolerance || 1e-12;
        this.safetyFactor = config.safetyFactor || 0.9;
        this.minScale = config.minScale || 0.2;
        this.maxScale = config.maxScale || 5.0;
        this.minStepSize = config.minStepSize || 1e-10;
        this.maxStepSize = config.maxStepSize || 0.1;
        this.maxIterations = config.maxIterations || 100;
        this.maxValue = config.maxValue || 1e10;
    }

    isFinite(v) {
        return typeof v === 'number' && !isNaN(v) && isFinite(v);
    }

    isFiniteArray(arr) {
        return arr.every(v => this.isFinite(v));
    }

    calculateLocalError(yNew, yOld, h, order) {
        const factor = 1 / (Math.pow(2, order) - 1);
        const errors = [];

        for (let i = 0; i < yNew.length; i++) {
            const diff = yNew[i] - yOld[i];
            if (this.isFinite(diff)) {
                errors.push(Math.abs(diff) * factor);
            } else {
                errors.push(Infinity);
            }
        }

        return errors;
    }

    calculateTruncationError(k, h, order) {
        let maxError = 0;

        for (let i = 0; i < k.length; i++) {
            if (this.isFinite(k[i])) {
                const error = Math.abs(k[i]) * Math.pow(h, order + 1);
                if (this.isFinite(error)) {
                    maxError = Math.max(maxError, error);
                }
            }
        }

        return maxError;
    }

    estimateTruncationErrorCoefficient(kList, h) {
        if (kList.length < 2) return 0;

        const k1 = kList[0];
        const k2 = kList[kList.length - 1];

        let maxCoeff = 0;
        for (let i = 0; i < k1.length; i++) {
            const diff = k2[i] - k1[i];
            if (this.isFinite(diff)) {
                const coeff = Math.abs(diff) / (kList.length - 1);
                maxCoeff = Math.max(maxCoeff, coeff);
            }
        }

        return maxCoeff;
    }

    estimateRoundOffError(h, y, order) {
        const machineEpsilon = 2.220446049250313e-16;
        let maxError = 0;

        for (let i = 0; i < y.length; i++) {
            if (this.isFinite(y[i])) {
                const error = machineEpsilon * Math.abs(y[i]) * (1 + Math.abs(h));
                if (this.isFinite(error)) {
                    maxError = Math.max(maxError, error);
                }
            }
        }

        return maxError;
    }

    calculateTotalError(truncationError, roundOffError) {
        if (!this.isFinite(truncationError) || !this.isFinite(roundOffError)) {
            return Infinity;
        }
        return Math.sqrt(truncationError * truncationError + roundOffError * roundOffError);
    }

    calculateStepSizeController(h, error, order) {
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

    calculateOptimalStepSize(h, error, order) {
        return this.calculateStepSizeController(h, error, order);
    }

    adaptiveStepSize(h, error, order) {
        return this.calculateStepSizeController(h, error, order);
    }

    richardsonExtrapolation(values, stepSize, order) {
        if (values.length < 2) return null;

        const factor = Math.pow(2, order);
        const results = [];

        for (let i = 0; i < values.length - 1; i++) {
            const fine = values[i + 1];
            const coarse = values[i];

            const extrapolated = fine.map((v, idx) => {
                const result = (factor * v - coarse[idx]) / (factor - 1);
                if (!this.isFinite(result) || Math.abs(result) > this.maxValue) {
                    return v;
                }
                return result;
            });
            results.push(extrapolated);
        }

        return results;
    }

    rombergExtrapolation(values, stepSize, order, maxOrder) {
        const table = [[...values]];

        for (let k = 1; k <= maxOrder; k++) {
            const row = [];
            const factor = Math.pow(4, k);

            for (let i = 0; i < table[k - 1].length - 1; i++) {
                const v1 = table[k - 1][i + 1];
                const v2 = table[k - 1][i];

                const extrapolated = v1.map((v, idx) => {
                    const result = (factor * v - v2[idx]) / (factor - 1);
                    if (!this.isFinite(result) || Math.abs(result) > this.maxValue) {
                        return v;
                    }
                    return result;
                });
                row.push(extrapolated);
            }

            table.push(row);
        }

        return table;
    }

    calculateConvergenceOrder(errors) {
        if (errors.length < 2) return 0;

        const ratios = [];
        for (let i = 1; i < errors.length - 1; i++) {
            if (!this.isFinite(errors[i]) || errors[i] === 0) continue;
            if (!this.isFinite(errors[i - 1]) || errors[i - 1] === 0) continue;

            const ratio = Math.log2(Math.abs(errors[i - 1] / errors[i]));
            if (this.isFinite(ratio)) {
                ratios.push(ratio);
            }
        }

        if (ratios.length === 0) return 0;
        return ratios.reduce((a, b) => a + b, 0) / ratios.length;
    }

    isConverged(currentError, previousError, tolerance = this.tolerance) {
        if (!this.isFinite(currentError)) return false;
        if (previousError === 0 || !this.isFinite(previousError)) return currentError < tolerance;

        const ratio = currentError / previousError;
        return ratio < 0.5 || currentError < tolerance;
    }

    calculateRelativeError(approximate, exact) {
        const errors = [];

        for (let i = 0; i < approximate.length; i++) {
            if (!this.isFinite(approximate[i]) || !this.isFinite(exact[i])) {
                errors.push(Infinity);
                continue;
            }

            if (Math.abs(exact[i]) < this.absoluteTolerance) {
                errors.push(Math.abs(approximate[i] - exact[i]));
            } else {
                const relativeError = Math.abs((approximate[i] - exact[i]) / exact[i]);
                errors.push(this.isFinite(relativeError) ? relativeError : Infinity);
            }
        }

        return errors;
    }

    calculateAbsoluteError(approximate, exact) {
        const errors = [];

        for (let i = 0; i < approximate.length; i++) {
            if (!this.isFinite(approximate[i]) || !this.isFinite(exact[i])) {
                errors.push(Infinity);
            } else {
                errors.push(Math.abs(approximate[i] - exact[i]));
            }
        }

        return errors;
    }

    calculateErrorNorm(errors) {
        let sumSquared = 0;

        for (let i = 0; i < errors.length; i++) {
            if (this.isFinite(errors[i])) {
                sumSquared += errors[i] * errors[i];
            } else {
                return Infinity;
            }
        }

        return Math.sqrt(sumSquared);
    }

    calculateMaxError(errors) {
        let maxError = 0;
        for (let i = 0; i < errors.length; i++) {
            if (this.isFinite(errors[i])) {
                maxError = Math.max(maxError, errors[i]);
            }
        }
        return maxError;
    }

    calculateMeanError(errors) {
        if (errors.length === 0) return 0;

        let sum = 0;
        let count = 0;
        for (let i = 0; i < errors.length; i++) {
            if (this.isFinite(errors[i])) {
                sum += errors[i];
                count++;
            }
        }

        return count > 0 ? sum / count : Infinity;
    }

    calculateRMSError(approximate, exact) {
        const absoluteErrors = this.calculateAbsoluteError(approximate, exact);
        return this.calculateErrorNorm(absoluteErrors);
    }

    estimateStabilityRegion(method, h, lambda) {
        const z = lambda * h;

        let value;
        switch (method) {
            case 'euler':
                value = 1 + z;
                break;

            case 'euler_improved':
                value = 1 + z + z * z / 2;
                break;

            case 'rk2':
                value = 1 + z + z * z / 2;
                break;

            case 'rk3':
                value = 1 + z + z * z / 2 + z * z * z / 6;
                break;

            case 'rk4':
                value = 1 + z + z * z / 2 + z * z * z / 6 + z * z * z * z / 24;
                break;

            default:
                value = 1 + z;
        }

        if (!this.isFinite(value)) return false;
        return Math.abs(value) <= 1;
    }

    calculateStabilityLimit(lambda, h) {
        const value = 1 + lambda * h;
        return this.isFinite(value) ? Math.abs(value) : Infinity;
    }

    isStable(lambda, h, method = 'rk4') {
        return this.estimateStabilityRegion(method, h, lambda);
    }

    calculateErrorTrend(results, exactSolution = null) {
        const trend = [];

        for (let i = 0; i < results.length; i++) {
            const point = results[i];

            if (!this.isFiniteArray(point.y)) {
                trend.push({
                    t: point.t,
                    errors: new Array(point.y.length).fill(Infinity),
                    maxError: Infinity,
                    rmsError: Infinity
                });
                continue;
            }

            if (exactSolution) {
                try {
                    const exact = exactSolution(point.t);
                    const exactValues = Array.isArray(exact) ? exact : [exact];
                    const errors = this.calculateAbsoluteError(point.y, exactValues);
                    trend.push({
                        t: point.t,
                        errors,
                        maxError: this.calculateMaxError(errors),
                        rmsError: this.calculateErrorNorm(errors)
                    });
                } catch (e) {
                    trend.push({
                        t: point.t,
                        errors: new Array(point.y.length).fill(Infinity),
                        maxError: Infinity,
                        rmsError: Infinity
                    });
                }
            } else if (i > 0) {
                const prevPoint = results[i - 1];
                const h = point.t - prevPoint.t;
                const errors = [];

                for (let j = 0; j < point.y.length; j++) {
                    if (this.isFinite(point.y[j]) && this.isFinite(prevPoint.y[j])) {
                        const derivative = (point.y[j] - prevPoint.y[j]) / Math.max(Math.abs(h), 1e-10);
                        const error = Math.abs(derivative) * h * h;
                        errors.push(this.isFinite(error) ? error : Infinity);
                    } else {
                        errors.push(Infinity);
                    }
                }

                trend.push({
                    t: point.t,
                    errors,
                    maxError: this.calculateMaxError(errors),
                    rmsError: this.calculateErrorNorm(errors)
                });
            } else {
                trend.push({
                    t: point.t,
                    errors: new Array(results[0].y.length).fill(0),
                    maxError: 0,
                    rmsError: 0
                });
            }
        }

        return trend;
    }
}

if (typeof module !== 'undefined' && module.exports) {
    module.exports = ErrorController;
}
