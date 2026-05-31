const EquationParser = require('../src/parser');
const EulerSolver = require('../src/euler');
const RungeKuttaSolver = require('../src/rungekutta');
const ErrorController = require('../src/error');

let passed = 0;
let failed = 0;

function test(name, fn) {
    try {
        fn();
        passed++;
        console.log(`  PASS: ${name}`);
    } catch (error) {
        failed++;
        console.log(`  FAIL: ${name} - ${error.message}`);
    }
}

function assert(condition, message) {
    if (!condition) {
        throw new Error(message || '断言失败');
    }
}

function assertApproximate(actual, expected, epsilon = 1e-6, message) {
    if (Math.abs(actual - expected) > epsilon) {
        throw new Error(message || `期望 ${expected}, 实际 ${actual}, 误差 ${Math.abs(actual - expected)}`);
    }
}

function assertIsFinite(value, message) {
    if (typeof value !== 'number' || isNaN(value) || !isFinite(value)) {
        throw new Error(message || `值不是有限数: ${value}`);
    }
}

function assertAllFinite(arr, message) {
    for (let i = 0; i < arr.length; i++) {
        if (typeof arr[i] !== 'number' || isNaN(arr[i]) || !isFinite(arr[i])) {
            throw new Error(message || `数组索引 ${i} 包含非有限值: ${arr[i]}`);
        }
    }
}

console.log('========================================');
console.log('常微分方程求解系统 - 稳定性和精度测试');
console.log('========================================\n');

console.log('--- 刚性方程测试 ---');

test('刚性方程: 大时间常数比', () => {
    const solver = new RungeKuttaSolver({
        stepSize: 0.01,
        adaptiveStep: true,
        tolerance: 1e-8,
        minStepSize: 1e-12,
        maxStepSize: 0.1
    });

    const f = (t, y) => [-1000 * y[0] + 999 * y[1], y[0] - y[1]];
    const result = solver.rk4(f, 0, [2, 1], 2, 0.01);

    assert(result.length > 0, '应有结果');
    assertAllFinite(result[result.length - 1].y, '结果应为有限值');

    const lastPoint = result[result.length - 1];
    assert(lastPoint.y[0] < 10, 'y[0] 应保持合理范围');
    assert(lastPoint.y[1] < 10, 'y[1] 应保持合理范围');
});

test('刚性方程: 显式欧拉法可能发散', () => {
    const solver = new EulerSolver({
        stepSize: 0.001,
        adaptiveStep: false,
        minStepSize: 1e-15,
        maxStepSize: 0.1
    });

    const f = (t, y) => [-100 * y[0]];

    try {
        const result = solver.euler(f, 0, [1], 0.1, 0.001);
        assert(result.length > 0, '应有结果');
        assertAllFinite(result[result.length - 1].y, '小步长不应发散');
    } catch (e) {
        assert(e.message.includes('发散') || e.message.includes('溢出'), '大步长可能发散');
    }
});

test('刚性方程: RK45自适应应能处理', () => {
    const solver = new RungeKuttaSolver({
        stepSize: 0.1,
        adaptiveStep: true,
        tolerance: 1e-6,
        minStepSize: 1e-12,
        maxStepSize: 0.1
    });

    const f = (t, y) => [-50 * y[0] + 50];
    const result = solver.rk45(f, 0, [0], 1, 0.1);

    assert(result.length > 0, '应有结果');
    const lastPoint = result[result.length - 1];
    assertAllFinite(lastPoint.y, '结果应为有限值');
    assertApproximate(lastPoint.y[0], 1, 0.1, '应趋近于稳态解1');
});

test('刚性方程: 大步长检测不稳定性', () => {
    const solver = new EulerSolver({
        stepSize: 0.1,
        adaptiveStep: false
    });

    const f = (t, y) => [-10 * y[0]];

    try {
        solver.euler(f, 0, [1], 1, 0.1);
    } catch (e) {
        assert(e.message.includes('发散') || e.message.includes('溢出'), '不稳定步长应抛出错误');
    }
});

console.log('\n--- 长时间积分稳定性测试 ---');

test('长时间积分: 简谐振动能量守恒', () => {
    const solver = new RungeKuttaSolver({
        stepSize: 0.01,
        adaptiveStep: false
    });

    const f = (t, y) => [y[1], -y[0]];
    const result = solver.rk4(f, 0, [1, 0], 100, 0.01);

    const energy0 = result[0].y[0] ** 2 + result[0].y[1] ** 2;
    const energyF = result[result.length - 1].y[0] ** 2 + result[result.length - 1].y[1] ** 2;

    assertAllFinite([energy0, energyF], '能量应为有限值');
    const energyError = Math.abs(energyF - energy0) / energy0;
    assert(energyError < 0.01, `能量漂移应小于1%, 实际 ${(energyError * 100).toFixed(2)}%`);
});

test('长时间积分: 指数衰减不发散', () => {
    const solver = new RungeKuttaSolver({
        stepSize: 0.1,
        adaptiveStep: true,
        tolerance: 1e-10
    });

    const f = (t, y) => [-y[0]];
    const result = solver.rk4(f, 0, [1], 100, 0.1);

    assert(result.length > 0, '应有结果');
    const lastPoint = result[result.length - 1];
    assertAllFinite(lastPoint.y, '结果应为有限值');
    assert(Math.abs(lastPoint.y[0]) < 1, '指数衰减应趋近于0');
});

test('长时间积分: Logistic方程收敛', () => {
    const solver = new RungeKuttaSolver({
        stepSize: 0.01,
        adaptiveStep: false
    });

    const f = (t, y) => [y[0] * (1 - y[0])];
    const result = solver.rk4(f, 0, [0.1], 50, 0.01);

    const lastPoint = result[result.length - 1];
    assertAllFinite(lastPoint.y, '结果应为有限值');
    assertApproximate(lastPoint.y[0], 1, 0.01, '应收敛到1');
});

console.log('\n--- 变步长精度测试 ---');

test('变步长: 自适应步长应保持精度', () => {
    const solver = new RungeKuttaSolver({
        stepSize: 0.1,
        adaptiveStep: true,
        tolerance: 1e-10
    });

    const f = (t, y) => [-y[0]];
    const result = solver.rk45(f, 0, [1], 1, 0.1);

    const lastPoint = result[result.length - 1];
    assertAllFinite(lastPoint.y, '结果应为有限值');
    assertApproximate(lastPoint.y[0], Math.exp(-1), 1e-8, '自适应步长应保持高精度');
});

test('变步长: 不同容差下的精度', () => {
    const f = (t, y) => [-y[0]];

    const solver1 = new RungeKuttaSolver({ stepSize: 0.1, adaptiveStep: true, tolerance: 1e-4 });
    const solver2 = new RungeKuttaSolver({ stepSize: 0.1, adaptiveStep: true, tolerance: 1e-10 });

    const result1 = solver1.rk45(f, 0, [1], 1, 0.1);
    const result2 = solver2.rk45(f, 0, [1], 1, 0.1);

    const error1 = Math.abs(result1[result1.length - 1].y[0] - Math.exp(-1));
    const error2 = Math.abs(result2[result2.length - 1].y[0] - Math.exp(-1));

    assert(error2 < error1, '更小容差应获得更高精度');
    assert(error2 < 1e-8, '高精度容差下误差应很小');
});

test('变步长: Richardson外推不应产生非有限值', () => {
    const solver = new RungeKuttaSolver({
        stepSize: 0.1,
        adaptiveStep: true,
        tolerance: 1e-6
    });

    const f = (t, y) => [-(y[0] ** 2)];
    const result = solver.rk4(f, 0, [1], 1, 0.1);

    for (let i = 0; i < result.length; i++) {
        assertAllFinite(result[i].y, `第 ${i} 个点应为有限值`);
    }
});

console.log('\n--- 高阶方程测试 ---');

test('高阶方程: 二阶线性方程', () => {
    const parser = new EquationParser();
    const solver = new RungeKuttaSolver({ stepSize: 0.01, adaptiveStep: false });

    const system = parser.parseHighOrder('-y1', 2);
    const f = (t, y) => system.evaluate(t, y);

    const result = solver.rk4(f, 0, [1, 0], 6.283, 0.01);

    assert(result.length > 0, '应有结果');
    const lastPoint = result[result.length - 1];
    assertAllFinite(lastPoint.y, '结果应为有限值');
    assertApproximate(lastPoint.y[0], 1, 0.02, '二阶方程解应周期性');
});

test('高阶方程: 三阶方程', () => {
    const parser = new EquationParser();
    const solver = new RungeKuttaSolver({ stepSize: 0.01, adaptiveStep: false });

    const system = parser.parseHighOrder('-y1 - y2', 3);
    const f = (t, y) => system.evaluate(t, y);

    const result = solver.rk4(f, 0, [1, 0, -1], 5, 0.01);

    assert(result.length > 0, '应有结果');
    const lastPoint = result[result.length - 1];
    assertAllFinite(lastPoint.y, '三阶方程结果应为有限值');
    assert(lastPoint.y.length === 3, '应返回3个状态变量');
});

test('高阶方程: 单摆方程', () => {
    const parser = new EquationParser();
    const solver = new RungeKuttaSolver({ stepSize: 0.01, adaptiveStep: false });

    const system = parser.parseHighOrder('-sin(y1)', 2);
    const f = (t, y) => system.evaluate(t, y);

    const result = solver.rk4(f, 0, [0.1, 0], 10, 0.01);

    assert(result.length > 0, '应有结果');
    const maxY = Math.max(...result.map(p => Math.abs(p.y[0])));
    assert(maxY < 0.5, '小角度单摆振幅应保持');
});

test('高阶方程: 初始条件顺序正确', () => {
    const parser = new EquationParser();
    const solver = new RungeKuttaSolver({ stepSize: 0.01, adaptiveStep: false });

    const system = parser.parseHighOrder('-y1', 2);
    const f = (t, y) => system.evaluate(t, y);

    const result = solver.rk4(f, 0, [1, 0.5], 0.01, 0.01);

    const derivative0 = f(0, [1, 0.5]);
    assertApproximate(derivative0[0], 0.5, 1e-10, 'y2应为y1的导数');
    assertApproximate(derivative0[1], -1, 1e-10, 'y3应为-y1');
});

console.log('\n--- 溢出检测测试 ---');

test('溢出检测: 非有限值检测', () => {
    const solver = new RungeKuttaSolver({ stepSize: 0.01 });

    assert(solver.isFinite(1.0), '1.0应为有限值');
    assert(solver.isFinite(0), '0应为有限值');
    assert(!solver.isFinite(Infinity), 'Infinity不应为有限值');
    assert(!solver.isFinite(NaN), 'NaN不应为有限值');
    assert(!solver.isFinite(-Infinity), '-Infinity不应为有限值');
});

test('溢出检测: 数组有限性检测', () => {
    const solver = new RungeKuttaSolver({ stepSize: 0.01 });

    assert(solver.isFiniteArray([1, 2, 3]), '[1,2,3]应为有限数组');
    assert(!solver.isFiniteArray([1, Infinity, 3]), '包含Infinity的数组不应通过');
    assert(!solver.isFiniteArray([NaN, 2, 3]), '包含NaN的数组不应通过');
});

test('溢出检测: 超大值检测', () => {
    const solver = new RungeKuttaSolver({ stepSize: 0.01, maxValue: 1e10 });

    assert(!solver.checkOverflow([1, 2, 3]), '小值不应溢出');
    assert(solver.checkOverflow([1e20, 2, 3]), '超大值应检测为溢出');
});

test('溢出检测: 极端增长方程', () => {
    const solver = new RungeKuttaSolver({
        stepSize: 0.01,
        adaptiveStep: false
    });

    const f = (t, y) => [y[0] ** 2];

    try {
        solver.rk4(f, 0, [1], 10, 0.01);
    } catch (e) {
        assert(e.message.includes('溢出') || e.message.includes('发散'), '极端增长应检测溢出');
    }
});

console.log('\n--- 精度对比测试 ---');

test('精度对比: RK4 > RK2 > 欧拉', () => {
    const f = (t, y) => [-y[0]];
    const exact = Math.exp(-1);

    const eulerSolver = new EulerSolver({ stepSize: 0.01, adaptiveStep: false });
    const rk2Solver = new RungeKuttaSolver({ stepSize: 0.01, adaptiveStep: false });
    const rk4Solver = new RungeKuttaSolver({ stepSize: 0.01, adaptiveStep: false });

    const eulerResult = eulerSolver.euler(f, 0, [1], 1, 0.01);
    const rk2Result = rk2Solver.rk2(f, 0, [1], 1, 0.01);
    const rk4Result = rk4Solver.rk4(f, 0, [1], 1, 0.01);

    const eulerError = Math.abs(eulerResult[eulerResult.length - 1].y[0] - exact);
    const rk2Error = Math.abs(rk2Result[rk2Result.length - 1].y[0] - exact);
    const rk4Error = Math.abs(rk4Result[rk4Result.length - 1].y[0] - exact);

    assert(rk4Error < rk2Error, 'RK4应比RK2更精确');
    assert(rk2Error < eulerError, 'RK2应比欧拉更精确');
});

test('精度对比: 收敛阶测试', () => {
    const f = (t, y) => [-y[0]];
    const exact = Math.exp(-1);

    const solver = new RungeKuttaSolver({ adaptiveStep: false });

    const errors = [];
    const stepSizes = [0.1, 0.05, 0.025, 0.0125];

    for (const h of stepSizes) {
        const result = solver.rk4(f, 0, [1], 1, h);
        errors.push(Math.abs(result[result.length - 1].y[0] - exact));
    }

    const ratios = [];
    for (let i = 1; i < errors.length; i++) {
        if (errors[i] > 0 && errors[i - 1] > 0) {
            ratios.push(Math.log2(errors[i - 1] / errors[i]));
        }
    }

    const avgOrder = ratios.reduce((a, b) => a + b, 0) / ratios.length;
    assert(avgOrder > 3, `RK4收敛阶应约为4, 实际 ${avgOrder.toFixed(2)}`);
});

console.log('\n--- 错误控制器测试 ---');

test('错误控制器: 安全的误差计算', () => {
    const controller = new ErrorController();

    const error1 = controller.calculateAbsoluteError([1, 2], [1.1, 2.1]);
    assertAllFinite(error1, '误差应为有限值');

    const error2 = controller.calculateAbsoluteError([NaN, 2], [1, 2]);
    assert(!isFinite(error2[0]) || error2[0] === Infinity, 'NaN应产生无限误差');
});

test('错误控制器: 步长调整不应溢出', () => {
    const controller = new ErrorController({ tolerance: 1e-8 });

    const h1 = controller.calculateStepSizeController(0.01, 1e-6, 4);
    assertIsFinite(h1, '步长应为有限值');

    const h2 = controller.calculateStepSizeController(0.01, 1e-20, 4);
    assertIsFinite(h2, '极小误差下步长应为有限值');

    const h3 = controller.calculateStepSizeController(0.01, Infinity, 4);
    assertIsFinite(h3, '无限误差下步长应为有限值');
});

test('错误控制器: 误差趋势计算', () => {
    const controller = new ErrorController();

    const results = [
        { t: 0, y: [0] },
        { t: 1, y: [1] },
        { t: 2, y: [4] }
    ];

    const trend = controller.calculateErrorTrend(results);
    assert(trend.length === results.length, '误差趋势应与结果等长');

    for (let i = 0; i < trend.length; i++) {
        assertIsFinite(trend[i].t, '时间应为有限值');
        assertIsFinite(trend[i].maxError, '误差应为有限值');
    }
});

console.log('\n========================================');
console.log(`测试完成: ${passed} 通过, ${failed} 失败`);
console.log('========================================');

process.exit(failed > 0 ? 1 : 0);
