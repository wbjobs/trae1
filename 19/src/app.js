class ODESolverApp {
    constructor() {
        this.parser = null;
        this.eulerSolver = null;
        this.rkSolver = null;
        this.errorController = null;
        this.renderer = null;
        this.errorRenderer = null;
        this.currentResult = null;
        this.errorTrend = null;
        this.config = { ...Config.defaultConfig };

        this.init();
    }

    init() {
        this.parser = new EquationParser();
        this.eulerSolver = new EulerSolver(this.config);
        this.rkSolver = new RungeKuttaSolver(this.config);
        this.errorController = new ErrorController(this.config);

        try {
            this.renderer = new Renderer('solutionCanvas');
            this.errorRenderer = new Renderer('errorCanvas');
        } catch (e) {
            console.warn('Canvas初始化失败（可能在Node.js环境中）:', e.message);
        }

        this.bindEvents();
        this.loadDefaultEquation();
    }

    bindEvents() {
        if (typeof document === 'undefined') return;

        const solveBtn = document.getElementById('solveBtn');
        if (solveBtn) {
            solveBtn.addEventListener('click', () => this.solve());
        }

        const compareBtn = document.getElementById('compareBtn');
        if (compareBtn) {
            compareBtn.addEventListener('click', () => this.compareMethods());
        }

        const errorBtn = document.getElementById('errorBtn');
        if (errorBtn) {
            errorBtn.addEventListener('click', () => this.showErrorAnalysis());
        }

        const methodSelect = document.getElementById('methodSelect');
        if (methodSelect) {
            methodSelect.addEventListener('change', () => this.updateMethodConfig());
        }

        const stepSizeInput = document.getElementById('stepSize');
        if (stepSizeInput) {
            stepSizeInput.addEventListener('change', (e) => {
                this.config.stepSize = parseFloat(e.target.value) || 0.01;
            });
        }

        const toleranceInput = document.getElementById('tolerance');
        if (toleranceInput) {
            toleranceInput.addEventListener('change', (e) => {
                this.config.tolerance = parseFloat(e.target.value) || 1e-8;
            });
        }

        const adaptiveCheckbox = document.getElementById('adaptiveStep');
        if (adaptiveCheckbox) {
            adaptiveCheckbox.addEventListener('change', (e) => {
                this.config.adaptiveStep = e.target.checked;
            });
        }
    }

    loadDefaultEquation() {
        if (typeof document === 'undefined') return;

        const equationInput = document.getElementById('equation');
        if (equationInput) {
            equationInput.value = '-2*y + exp(-t)';
        }

        const initialConditions = document.getElementById('initialConditions');
        if (initialConditions) {
            initialConditions.value = '1';
        }

        const tStart = document.getElementById('tStart');
        if (tStart) {
            tStart.value = '0';
        }

        const tEnd = document.getElementById('tEnd');
        if (tEnd) {
            tEnd.value = '5';
        }
    }

    updateMethodConfig() {
        const methodSelect = document.getElementById('methodSelect');
        if (methodSelect) {
            this.config.method = methodSelect.value;
        }

        this.eulerSolver = new EulerSolver(this.config);
        this.rkSolver = new RungeKuttaSolver(this.config);
        this.errorController = new ErrorController(this.config);
    }

    parseInput() {
        const equationInput = document.getElementById('equation');
        const initialConditionsInput = document.getElementById('initialConditions');
        const tStartInput = document.getElementById('tStart');
        const tEndInput = document.getElementById('tEnd');

        if (!equationInput || !initialConditionsInput || !tStartInput || !tEndInput) {
            throw new Error('无法获取输入元素');
        }

        const expression = equationInput.value.trim();
        const initialConditionsStr = initialConditionsInput.value.trim();
        const tStart = parseFloat(tStartInput.value);
        const tEnd = parseFloat(tEndInput.value);

        const initialConditions = initialConditionsStr
            .split(',')
            .map(s => parseFloat(s.trim()))
            .filter(v => !isNaN(v));

        const order = initialConditions.length;

        let equation;
        if (order === 1) {
            equation = this.parser.parse(expression);
        } else {
            equation = this.parser.parseHighOrder(expression, order);
        }

        return { equation, initialConditions, tStart, tEnd, order };
    }

    solve() {
        try {
            const { equation, initialConditions, tStart, tEnd } = this.parseInput();
            const method = this.config.method;

            let result;
            if (method.startsWith('euler') || method === 'midpoint' || method === 'heun') {
                result = this.eulerSolver.solve(equation, initialConditions, tStart, tEnd, method);
            } else {
                result = this.rkSolver.solve(equation, initialConditions, tStart, tEnd, method);
            }

            this.currentResult = result;
            this.errorTrend = this.errorController.calculateErrorTrend(result);

            if (this.renderer) {
                this.renderer.renderSolution(result, {
                    title: this.getMethodTitle(method),
                    color: '#2196F3'
                });
            }

            this.displayResults(result);

            if (this.errorRenderer) {
                this.errorRenderer.renderErrorTrend(this.errorTrend, {
                    title: '误差趋势'
                });
            }

        } catch (error) {
            console.error('求解失败:', error);
            this.showError(error.message);
        }
    }

    compareMethods() {
        try {
            const { equation, initialConditions, tStart, tEnd } = this.parseInput();

            const methods = [
                { key: 'euler', label: '欧拉法', color: '#F44336' },
                { key: 'euler_improved', label: '改进欧拉法', color: '#FF9800' },
                { key: 'rk4', label: '龙格-库塔4阶', color: '#2196F3' },
                { key: 'rk45', label: '龙格-库塔45阶', color: '#4CAF50' }
            ];

            const datasets = methods.map(m => {
                let result;
                if (m.key.startsWith('euler')) {
                    result = this.eulerSolver.solve(equation, initialConditions, tStart, tEnd, m.key);
                } else {
                    result = this.rkSolver.solve(equation, initialConditions, tStart, tEnd, m.key);
                }
                return {
                    data: result,
                    label: m.label,
                    color: m.color
                };
            });

            if (this.renderer) {
                this.renderer.renderMultipleSolutions(datasets, {
                    title: '多种方法对比'
                });
            }

            this.showComparison(datasets);

        } catch (error) {
            console.error('对比失败:', error);
            this.showError(error.message);
        }
    }

    showErrorAnalysis() {
        try {
            if (!this.currentResult) {
                this.solve();
            }

            if (!this.currentResult) return;

            const errorTrend = this.errorController.calculateErrorTrend(this.currentResult);
            this.errorTrend = errorTrend;

            if (this.errorRenderer) {
                this.errorRenderer.renderErrorTrend(errorTrend, {
                    title: '误差趋势分析'
                });
            }

            this.displayErrorStatistics(errorTrend);

        } catch (error) {
            console.error('误差分析失败:', error);
            this.showError(error.message);
        }
    }

    displayResults(result) {
        const resultDiv = document.getElementById('resultDisplay');
        if (!resultDiv) return;

        let html = '<div class="result-panel">';
        html += '<h3>求解结果</h3>';
        html += '<table class="result-table">';
        html += '<thead><tr><th>t</th><th>y(t)</th></tr></thead>';
        html += '<tbody>';

        const displayCount = Math.min(result.length, 20);
        const step = Math.max(1, Math.floor(result.length / displayCount));

        for (let i = 0; i < result.length; i += step) {
            const point = result[i];
            html += `<tr><td>${point.t.toFixed(4)}</td><td>${point.y[0].toFixed(6)}</td></tr>`;
        }

        html += '</tbody></table>';
        html += `<p>共 ${result.length} 个数据点</p>`;
        html += '</div>';

        resultDiv.innerHTML = html;
    }

    showComparison(datasets) {
        const resultDiv = document.getElementById('resultDisplay');
        if (!resultDiv) return;

        let html = '<div class="result-panel">';
        html += '<h3>方法对比</h3>';
        html += '<table class="result-table">';
        html += '<thead><tr><th>t</th>';

        datasets.forEach(ds => {
            html += `<th>${ds.label}</th>`;
        });
        html += '</tr></thead><tbody>';

        const minLength = Math.min(...datasets.map(ds => ds.data.length));
        const displayCount = Math.min(minLength, 10);
        const step = Math.max(1, Math.floor(minLength / displayCount));

        for (let i = 0; i < minLength; i += step) {
            const t = datasets[0].data[i].t;
            html += `<tr><td>${t.toFixed(4)}</td>`;

            datasets.forEach(ds => {
                const y = ds.data[i].y[0];
                html += `<td>${y.toFixed(6)}</td>`;
            });

            html += '</tr>';
        }

        html += '</tbody></table>';
        html += '</div>';

        resultDiv.innerHTML = html;
    }

    displayErrorStatistics(errorTrend) {
        const errorDiv = document.getElementById('errorDisplay');
        if (!errorDiv) return;

        const maxError = Math.max(...errorTrend.map(e => e.maxError || 0));
        const avgError = errorTrend.reduce((sum, e) => sum + (e.maxError || 0), 0) / errorTrend.length;
        const finalError = errorTrend[errorTrend.length - 1]?.maxError || 0;

        let html = '<div class="error-panel">';
        html += '<h3>误差统计</h3>';
        html += '<ul>';
        html += `<li>最大误差: ${maxError.toExponential(6)}</li>`;
        html += `<li>平均误差: ${avgError.toExponential(6)}</li>`;
        html += `<li>终点误差: ${finalError.toExponential(6)}</li>`;
        html += `<li>数据点数: ${errorTrend.length}</li>`;
        html += '</ul>';
        html += '</div>';

        errorDiv.innerHTML = html;
    }

    showError(message) {
        const errorDiv = document.getElementById('errorDisplay');
        if (errorDiv) {
            errorDiv.innerHTML = `<div class="error-message">错误: ${message}</div>`;
        }
    }

    getMethodTitle(method) {
        const titles = {
            'euler': '欧拉法求解曲线',
            'euler_improved': '改进欧拉法求解曲线',
            'midpoint': '中点法求解曲线',
            'heun': 'Heun法求解曲线',
            'rk2': '龙格-库塔2阶求解曲线',
            'rk3': '龙格-库塔3阶求解曲线',
            'rk4': '龙格-库塔4阶求解曲线',
            'rk5': '龙格-库塔5阶求解曲线',
            'rk45': '龙格-库塔45阶求解曲线',
            'rkf45': '龙格-库塔Fehlberg45阶求解曲线'
        };
        return titles[method] || '数值求解曲线';
    }

    solveFromConfig(config) {
        const { equation, initialConditions, tStart, tEnd, method } = config;

        let parsedEquation;
        if (Array.isArray(initialConditions) && initialConditions.length > 1) {
            parsedEquation = this.parser.parseHighOrder(equation, initialConditions.length);
        } else {
            parsedEquation = this.parser.parse(equation);
        }

        const y0 = Array.isArray(initialConditions) ? initialConditions : [initialConditions];
        const selectedMethod = method || 'rk4';

        let result;
        if (selectedMethod.startsWith('euler') || selectedMethod === 'midpoint' || selectedMethod === 'heun') {
            const solver = new EulerSolver({
                stepSize: config.stepSize || 0.01,
                tolerance: config.tolerance || 1e-8,
                adaptiveStep: config.adaptiveStep !== false
            });
            result = solver.solve(parsedEquation, y0, tStart, tEnd, selectedMethod);
        } else {
            const solver = new RungeKuttaSolver({
                stepSize: config.stepSize || 0.01,
                tolerance: config.tolerance || 1e-8,
                adaptiveStep: config.adaptiveStep !== false
            });
            result = solver.solve(parsedEquation, y0, tStart, tEnd, selectedMethod);
        }

        return {
            result,
            errorTrend: this.errorController.calculateErrorTrend(result)
        };
    }
}

if (typeof module !== 'undefined' && module.exports) {
    module.exports = ODESolverApp;
}

if (typeof document !== 'undefined') {
    document.addEventListener('DOMContentLoaded', () => {
        window.odeApp = new ODESolverApp();
    });
}
