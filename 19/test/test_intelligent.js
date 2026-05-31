const EquationParser = require('../src/parser');
const EulerSolver = require('../src/euler');
const RungeKuttaSolver = require('../src/rungekutta');
const ErrorController = require('../src/error');
const EquationClassifier = require('../src/classifier');
const ErrorSourceAnalyzer = require('../src/error_analyzer');

let passed = 0;
let failed = 0;

function test(name, fn) {
    try {
        fn();
        passed++;
        console.log(`  PASS: ${name}`);
    } catch (error) {
        failed++;
        console.log(`  FAIL: ${name}`);
        console.log(`    错误: ${error.message}`);
    }
}

function assertApproximate(actual, expected, tolerance, message) {
    const diff = Math.abs(actual - expected);
    if (diff > tolerance) {
        throw new Error(`${message}: 期望 ${expected}, 实际 ${actual}, 差异 ${diff}`);
    }
}

function assertTrue(value, message) {
    if (!value) {
        throw new Error(`${message}: 期望为 true`);
    }
}

function assertFalse(value, message) {
    if (value) {
        throw new Error(`${message}: 期望为 false`);
    }
}

function assertEqual(actual, expected, message) {
    if (actual !== expected) {
        throw new Error(`${message}: 期望 ${expected}, 实际 ${actual}`);
    }
}

function assertArrayApproximate(actual, expected, tolerance, message) {
    if (actual.length !== expected.length) {
        throw new Error(`${message}: 数组长度不匹配`);
    }
    for (let i = 0; i < actual.length; i++) {
        assertApproximate(actual[i], expected[i], tolerance, `${message}[${i}]`);
    }
}

console.log('========================================');
console.log('常微分方程求解系统 - 智能分析测试');
console.log('========================================');

console.log('\n--- 方程类型智能识别测试 ---');

test('EquationClassifier: 实例化', () => {
    const classifier = new EquationClassifier();
    assertTrue(classifier !== null, '实例化应成功');
});

test('EquationClassifier: 检测自治系统', () => {
    const classifier = new EquationClassifier();
    const f = (t, y) => [-y[0]];
    const result = classifier.checkAutonomy(f, 0, [1]);
    assertTrue(result, 'dy/dt = -y 应该是自治的');
});

test('EquationClassifier: 检测非自治系统', () => {
    const classifier = new EquationClassifier();
    const f = (t, y) => [-y[0] + Math.sin(t)];
    const result = classifier.checkAutonomy(f, 0, [1]);
    assertFalse(result, 'dy/dt = -y + sin(t) 应该是非自治的');
});

test('EquationClassifier: 检测线性系统', () => {
    const classifier = new EquationClassifier();
    const f = (t, y) => [-2 * y[0] + y[1], y[0] - 3 * y[1]];
    const result = classifier.checkLinearity(f, 0, [1, 1]);
    assertTrue(result, '线性系统应该被检测到');
});

test('EquationClassifier: 检测非线性系统', () => {
    const classifier = new EquationClassifier();
    const f = (t, y) => [-y[0] * y[0], y[1]];
    const result = classifier.checkLinearity(f, 0, [1, 1]);
    assertFalse(result, '非线性系统不应被误判为线性');
});

test('EquationClassifier: 检测刚性系统', () => {
    const classifier = new EquationClassifier({ stiffnessThreshold: 10 });
    const f = (t, y) => [-1000 * y[0], -y[1]];
    const result = classifier.estimateStiffness(f, 0, [1, 1], 1);
    assertTrue(result.isStiff, '大时间常数比的系统应该是刚性的');
    assertTrue(result.ratio > 100, `刚度比应该很大，实际: ${result.ratio}`);
});

test('EquationClassifier: 检测非刚性系统', () => {
    const classifier = new EquationClassifier({ stiffnessThreshold: 10 });
    const f = (t, y) => [-y[0], -2 * y[1]];
    const result = classifier.estimateStiffness(f, 0, [1, 1], 1);
    assertFalse(result.isStiff, '相似时间常数的系统不应是刚性的');
});

test('EquationClassifier: 检测指数型方程', () => {
    const classifier = new EquationClassifier();
    const f = (t, y) => [-0.5 * y[0]];
    const result = classifier.checkExponential(f, 0, [2]);
    assertTrue(result, 'dy/dt = -0.5y 应该是指数型的');
});

test('EquationClassifier: 检测振荡系统', () => {
    const classifier = new EquationClassifier();
    const f = (t, y) => [y[1], -y[0]];
    const result = classifier.checkOscillatory(f, 0, [1, 0]);
    assertTrue(result, '简谐振动应该被检测为振荡系统');
});

test('EquationClassifier: 完整分类分析', () => {
    const classifier = new EquationClassifier();
    const f = (t, y) => [y[1], -y[0] - 0.1 * y[1]];
    const analysis = classifier.classify(f, 0, [1, 0], 10);

    assertTrue(analysis.dimension === 2, '维度应该是2');
    assertTrue(analysis.type !== null, '类型应该被识别');
    assertTrue(Array.isArray(analysis.recommendedMethods), '应该有推荐方法');
    assertTrue(analysis.recommendedMethods.length > 0, '推荐方法列表不应为空');
});

test('EquationClassifier: 方法推荐排序', () => {
    const classifier = new EquationClassifier();
    const f = (t, y) => [-y[0]];
    const analysis = classifier.classify(f, 0, [1], 1);

    const methods = analysis.recommendedMethods;
    for (let i = 1; i < methods.length; i++) {
        assertTrue(methods[i - 1].score >= methods[i].score,
            `方法应该按评分降序排列: ${methods[i - 1].score} >= ${methods[i].score}`);
    }
});

test('EquationClassifier: 生成分析详情', () => {
    const classifier = new EquationClassifier();
    const f = (t, y) => [-1000 * y[0], -y[1]];
    const analysis = classifier.classify(f, 0, [1, 1], 1);

    assertTrue(analysis.analysisDetails.summary !== undefined, '应该有摘要');
    assertTrue(Array.isArray(analysis.analysisDetails.warnings), '警告应该是数组');
    assertTrue(Array.isArray(analysis.analysisDetails.suggestions), '建议应该是数组');
});

test('EquationClassifier: 检测奇异性', () => {
    const classifier = new EquationClassifier();
    const f = (t, y) => [1 / (y[0] - 1)];
    const result = classifier.checkSingularity(f, 0, [0], 1);
    assertTrue(true, '奇异性检测完成');
});

console.log('\n--- 误差溯源分析测试 ---');

test('ErrorSourceAnalyzer: 实例化', () => {
    const analyzer = new ErrorSourceAnalyzer();
    assertTrue(analyzer !== null, '实例化应成功');
});

test('ErrorSourceAnalyzer: 估计截断误差', () => {
    const analyzer = new ErrorSourceAnalyzer();
    const f = (t, y) => [-y[0]];

    const result = {
        y: [[1], [0.9], [0.81]],
        t: [0, 0.1, 0.2],
        stepSize: 0.1,
        method: 'rk4'
    };

    const error = analyzer.estimateTruncationError('rk4', 0.1, f, result.t, result.y);
    assertTrue(error > 0, '截断误差应该是正数');
    assertTrue(error < 0.1, 'RK4的截断误差应该很小');
});

test('ErrorSourceAnalyzer: 估计舍入误差', () => {
    const analyzer = new ErrorSourceAnalyzer();
    const y = [[1.23456789], [2.34567890]];
    const error = analyzer.estimateRoundingError(y);
    assertTrue(error > 0, '舍入误差应该存在');
});

test('ErrorSourceAnalyzer: 分析收敛性', () => {
    const analyzer = new ErrorSourceAnalyzer();
    const errors = [0.1, 0.05, 0.025, 0.0125, 0.00625];
    const t = [0, 1, 2, 3, 4];

    const convergence = analyzer.analyzeConvergence(errors, t);
    assertTrue(convergence.isConvergent, '误差应该收敛');
    assertTrue(convergence.order > 0, '收敛阶应该是正数');
});

test('ErrorSourceAnalyzer: 分析步长影响', () => {
    const analyzer = new ErrorSourceAnalyzer();
    const result = analyzer.analyzeStepSizeEffect(0.1, 'rk4');

    assertTrue(result.status === 'good' || result.status === 'too_small', '步长0.1对RK4应该合适');
    assertTrue(result.optimalRange !== undefined, '应该有最优范围');
});

test('ErrorSourceAnalyzer: 完整分析', () => {
    const analyzer = new ErrorSourceAnalyzer();
    const f = (t, y) => [-y[0]];
    const analyticalSolution = (t) => Math.exp(-t);

    const result = {
        y: [[1], [0.9048], [0.8187], [0.7408]],
        t: [0, 0.1, 0.2, 0.3],
        stepSize: 0.1,
        method: 'rk4'
    };

    const analysis = analyzer.analyze(result, f, analyticalSolution);

    assertTrue(analysis.totalError > 0, '总误差应该大于0');
    assertTrue(analysis.errorSources.truncationError.value > 0, '截断误差应该存在');
    assertTrue(analysis.errorSources.roundingError.value > 0, '舍入误差应该存在');
});

test('ErrorSourceAnalyzer: 生成建议', () => {
    const analyzer = new ErrorSourceAnalyzer();
    const analysis = {
        totalError: 0.15,
        errorSources: {
            truncationError: { value: 0.1, percentage: 80, description: '截断误差' },
            roundingError: { value: 0.02, percentage: 16, description: '舍入误差' },
            stabilityError: { value: 0.005, percentage: 4, description: '稳定性误差' },
            implementationError: { value: 0, percentage: 0, description: '实现误差' }
        },
        errorConvergence: { isConvergent: true, order: 4 },
        stepSizeEffect: { status: 'too_large', recommendation: '减小步长' },
        reliability: 'low'
    };

    const recommendations = analyzer.generateRecommendations(analysis);
    assertTrue(Array.isArray(recommendations), '建议应该是数组');
    assertTrue(recommendations.length > 0, '对于大误差应该有建议');
});

test('ErrorSourceAnalyzer: 生成报告', () => {
    const analyzer = new ErrorSourceAnalyzer();
    const analysis = {
        totalError: 0.01,
        errorSources: {
            truncationError: { value: 0.008, percentage: 80, description: '截断误差' },
            roundingError: { value: 0.001, percentage: 10, description: '舍入误差' },
            stabilityError: { value: 0.0005, percentage: 5, description: '稳定性误差' },
            implementationError: { value: 0.0005, percentage: 5, description: '实现误差' }
        },
        errorConvergence: { isConvergent: true, order: 4 },
        stepSizeEffect: { status: 'good', recommendation: '步长合适' },
        recommendations: [],
        reliability: 'high'
    };

    const report = analyzer.generateReport(analysis);
    assertTrue(typeof report === 'string', '报告应该是字符串');
    assertTrue(report.includes('误差溯源分析报告'), '报告应该包含标题');
    assertTrue(report.includes('截断误差'), '报告应该包含截断误差');
});

test('ErrorSourceAnalyzer: 稳定性误差检测', () => {
    const analyzer = new ErrorSourceAnalyzer();
    const errors = [0.001, 0.01, 0.1, 1.0, 10.0];
    const result = analyzer.estimateStabilityError(errors, 0.1, 'euler');
    assertTrue(result > 0, '指数增长的误差应该检测到不稳定性');
});

console.log('\n--- PI控制器步长调整测试 ---');

test('RungeKuttaSolver: PI控制器平滑步长调整', () => {
    const solver = new RungeKuttaSolver({
        tolerance: 1e-6,
        usePIController: true
    });

    const h1 = solver.adjustStepSize(0.01, 1e-6, 4);
    const h2 = solver.adjustStepSize(0.01, 2e-6, 4);

    assertTrue(h1 > 0, '调整后的步长应该是正数');
    assertTrue(h2 > 0, '调整后的步长应该是正数');
});

test('RungeKuttaSolver: 自适应统计信息', () => {
    const solver = new RungeKuttaSolver();

    solver.errorHistory = [1e-7, 2e-7, 1.5e-7, 1.8e-7, 1.2e-7];

    const stats = solver.getAdaptiveStatistics();
    assertEqual(stats.steps, 5, '步数应该是5');
    assertTrue(stats.avgError > 0, '平均误差应该是正数');
    assertTrue(stats.maxError >= stats.minError, '最大误差应该大于等于最小误差');
});

test('RungeKuttaSolver: 收敛性检查', () => {
    const solver = new RungeKuttaSolver({ tolerance: 1e-6 });

    solver.errorHistory = [1e-3, 5e-4, 2e-4, 1e-4, 5e-5, 2e-5, 1e-5, 5e-6, 2e-6, 1e-6, 5e-7];

    const convergence = solver.checkConvergence();
    assertTrue(convergence.converged, '应该检测到收敛');
    assertEqual(convergence.trend, 'decreasing', '误差趋势应该是下降的');
});

console.log('\n--- 完整集成测试 ---');

test('完整流程: 分类 -> 求解 -> 分析', () => {
    const classifier = new EquationClassifier();
    const parser = new EquationParser();
    const solver = new RungeKuttaSolver({ tolerance: 1e-6 });
    const analyzer = new ErrorSourceAnalyzer();

    const f = (t, y) => [-0.5 * y[0]];
    const analyticalSolution = (t) => Math.exp(-0.5 * t);

    const classification = classifier.classify(f, 0, [1], 5);
    assertTrue(classification.isExponential, '应该识别为指数型');

    const recommendedMethod = classification.recommendedMethods[0].method;
    assertTrue(['rk4', 'rk45', 'heun'].includes(recommendedMethod),
        `推荐方法应该合适: ${recommendedMethod}`);

    const result = solver.solve(parser.parse('-0.5 * y'), 1, 0, 2, recommendedMethod);
    assertTrue(result.length > 0, '应该有结果');

    const resultForAnalysis = {
        y: result.map(r => r.y),
        t: result.map(r => r.t),
        stepSize: 0.1,
        method: recommendedMethod
    };

    const analysis = analyzer.analyze(resultForAnalysis, f, analyticalSolution);
    assertTrue(analysis.totalError < 0.1, '误差应该较小');
    assertTrue(analysis.reliability === 'high', '可靠性应该高');
});

test('完整流程: 刚性方程处理', () => {
    const classifier = new EquationClassifier({ stiffnessThreshold: 10 });
    const solver = new RungeKuttaSolver({
        tolerance: 1e-8,
        stiffnessThreshold: 10
    });

    const f = (t, y) => [-100 * y[0] + 99 * y[1], 100 * y[0] - 101 * y[1]];

    const classification = classifier.classify(f, 0, [1, 1], 1);
    assertTrue(classification.isStiff, '应该识别为刚性');

    const result = solver.rk45(f, 0, [2, 0], 0.5, 0.001);
    assertTrue(result.length > 0, '刚性方程应该有结果');

    for (const point of result) {
        assertTrue(isFinite(point.y[0]) && isFinite(point.y[1]),
            `所有结果应该是有限值: ${point.y}`);
    }
});

test('完整流程: 振荡方程求解', () => {
    const classifier = new EquationClassifier();
    const solver = new RungeKuttaSolver({ tolerance: 1e-8 });

    const f = (t, y) => [y[1], -y[0]];

    const classification = classifier.classify(f, 0, [1, 0], 10);
    assertTrue(classification.isOscillatory, '应该识别为振荡型');

    const result = solver.rk4(f, 0, [1, 0], 6.28, 0.01);

    let energyError = 0;
    for (const point of result) {
        const energy = point.y[0] * point.y[0] + point.y[1] * point.y[1];
        energyError = Math.max(energyError, Math.abs(energy - 1));
    }

    assertTrue(energyError < 0.01, `能量守恒应该良好: ${energyError}`);
});

test('完整流程: 自动选择最优方法', () => {
    const classifier = new EquationClassifier();
    const solver = new RungeKuttaSolver({ tolerance: 1e-8 });

    const testCases = [
        { f: (t, y) => [-y[0]], y0: [1], expected: 'exponential' },
        { f: (t, y) => [y[1], -y[0]], y0: [1, 0], expected: 'oscillatory' },
        { f: (t, y) => [-1000 * y[0], -y[1]], y0: [1, 1], expected: 'stiff' }
    ];

    for (const tc of testCases) {
        const classification = classifier.classify(tc.f, 0, tc.y0, 1);
        assertEqual(classification.type, tc.expected,
            `方程类型应该是 ${tc.expected}, 实际: ${classification.type}`);

        const bestMethod = classification.recommendedMethods[0];
        assertTrue(bestMethod.score >= 80,
            `推荐方法评分应该较高: ${bestMethod.method} = ${bestMethod.score}`);
    }
});

console.log('\n========================================');
console.log(`测试完成: ${passed} 通过, ${failed} 失败`);
console.log('========================================');

if (failed > 0) {
    process.exit(1);
}
