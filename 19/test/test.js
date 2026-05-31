const fs = require('fs');
const path = require('path');

const Config = require('../src/config');
const EquationParser = require('../src/parser');
const EulerSolver = require('../src/euler');
const RungeKuttaSolver = require('../src/rungekutta');
const ErrorController = require('../src/error');

let passed = 0;
let failed = 0;
const results = [];

function test(name, fn) {
    try {
        fn();
        passed++;
        results.push({ name, status: 'PASS' });
        console.log(`✓ ${name}`);
    } catch (error) {
        failed++;
        results.push({ name, status: 'FAIL', error: error.message });
        console.log(`✗ ${name}: ${error.message}`);
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

function assertArrayApproximate(actual, expected, epsilon = 1e-6, message) {
    if (actual.length !== expected.length) {
        throw new Error(`数组长度不匹配: 期望 ${expected.length}, 实际 ${actual.length}`);
    }
    for (let i = 0; i < actual.length; i++) {
        if (Math.abs(actual[i] - expected[i]) > epsilon) {
            throw new Error(message || `索引 ${i}: 期望 ${expected[i]}, 实际 ${actual[i]}`);
        }
    }
}

console.log('========================================');
console.log('常微分方程求解系统 - 模块测试');
console.log('========================================\n');

test('Config: 默认配置加载', () => {
    assert(typeof Config.defaultConfig === 'object', '默认配置应为对象');
    assert(Config.defaultConfig.stepSize === 0.01, '默认步长应为0.01');
    assert(Config.defaultConfig.tolerance === 1e-8, '默认容差应为1e-8');
});

test('Config: 方法枚举', () => {
    assert(Config.methods.EULER === 'euler', '欧拉方法');
    assert(Config.methods.RK4 === 'rk4', 'RK4方法');
    assert(Config.methods.RK45 === 'rk45', 'RK45方法');
});

test('EquationParser: 数字表达式解析', () => {
    const parser = new EquationParser();
    const eq = parser.parse('2 + 3');
    assert(typeof eq.evaluate === 'function', '应返回evaluate函数');
});

test('EquationParser: 简单变量表达式', () => {
    const parser = new EquationParser();
    const eq = parser.parse('t + y');
    const result = eq.evaluate(1, 2);
    assertApproximate(result, 3, 1e-10, 't+y 当t=1,y=2时应为3');
});

test('EquationParser: 指数函数', () => {
    const parser = new EquationParser();
    const eq = parser.parse('exp(t)');
    const result = eq.evaluate(0, 0);
    assertApproximate(result, 1, 1e-10, 'exp(0) 应为1');
});

test('EquationParser: 三角函数', () => {
    const parser = new EquationParser();
    const eq = parser.parse('sin(t) + cos(t)');
    const result = eq.evaluate(0, 0);
    assertApproximate(result, 1, 1e-10, 'sin(0)+cos(0) 应为1');
});

test('EquationParser: 复杂表达式', () => {
    const parser = new EquationParser();
    const eq = parser.parse('-2*y + exp(-t)');
    const result = eq.evaluate(0, 1);
    assertApproximate(result, -1, 1e-10, '-2*1+exp(0) 应为-1');
});

test('EquationParser: 幂运算', () => {
    const parser = new EquationParser();
    const eq = parser.parse('y^2');
    const result = eq.evaluate(0, 3);
    assertApproximate(result, 9, 1e-10, 'y^2 当y=3时应为9');
});

test('EquationParser: 常数PI和E', () => {
    const parser = new EquationParser();
    const eq = parser.parse('sin(pi/2)');
    const result = eq.evaluate(0, 0);
    assertApproximate(result, 1, 1e-10, 'sin(π/2) 应为1');
});

test('EquationParser: 函数解析', () => {
    const parser = new EquationParser();
    const fn = (t, y) => -y;
    const eq = parser.parse(fn);
    const result = eq.evaluate(0, 1);
    assertApproximate(result, -1, 1e-10, '函数解析应正确');
});

test('EquationParser: 高阶方程解析', () => {
    const parser = new EquationParser();
    const system = parser.parseHighOrder('-y', 2);
    assert(system.type === 'system', '应返回系统类型');
    assert(system.order === 2, '阶数应为2');
    assert(system.equations.length === 2, '应有2个方程');
});

test('EulerSolver: 实例化', () => {
    const solver = new EulerSolver({ stepSize: 0.01 });
    assert(solver.stepSize === 0.01, '步长设置正确');
});

test('EulerSolver: 欧拉法 - 指数衰减', () => {
    const solver = new EulerSolver({ stepSize: 0.001, adaptiveStep: false });
    const f = (t, y) => [-y[0]];
    const result = solver.euler(f, 0, [1], 1, 0.001);

    assert(result.length > 0, '应有结果');
    assertApproximate(result[0].y[0], 1, 1e-10, '初始值应为1');

    const lastPoint = result[result.length - 1];
    assertApproximate(lastPoint.t, 1, 0.01, '终点t应为1');
    assertApproximate(lastPoint.y[0], Math.exp(-1), 0.01, '终点y应接近exp(-1)');
});

test('EulerSolver: 改进欧拉法', () => {
    const solver = new EulerSolver({ stepSize: 0.01, adaptiveStep: false });
    const f = (t, y) => [-y[0]];
    const result = solver.eulerImproved(f, 0, [1], 1, 0.01);

    const lastPoint = result[result.length - 1];
    assertApproximate(lastPoint.y[0], Math.exp(-1), 0.001, '改进欧拉法精度应更高');
});

test('EulerSolver: 中点法', () => {
    const solver = new EulerSolver({ stepSize: 0.01, adaptiveStep: false });
    const f = (t, y) => [-y[0]];
    const result = solver.midpointMethod(f, 0, [1], 1, 0.01);

    const lastPoint = result[result.length - 1];
    assertApproximate(lastPoint.y[0], Math.exp(-1), 0.001, '中点法精度');
});

test('EulerSolver: Heun法', () => {
    const solver = new EulerSolver({ stepSize: 0.01, adaptiveStep: false });
    const f = (t, y) => [-y[0]];
    const result = solver.heunMethod(f, 0, [1], 1, 0.01);

    const lastPoint = result[result.length - 1];
    assertApproximate(lastPoint.y[0], Math.exp(-1), 0.001, 'Heun法精度');
});

test('EulerSolver: 自适应步长', () => {
    const solver = new EulerSolver({ stepSize: 0.1, adaptiveStep: true, tolerance: 1e-6 });
    const f = (t, y) => [-y[0]];
    const result = solver.eulerImproved(f, 0, [1], 1);

    const lastPoint = result[result.length - 1];
    assertApproximate(lastPoint.y[0], Math.exp(-1), 0.001, '自适应步长应收敛');
});

test('EulerSolver: solve接口', () => {
    const parser = new EquationParser();
    const solver = new EulerSolver({ stepSize: 0.01, adaptiveStep: false });
    const eq = parser.parse('-y');
    const result = solver.solve(eq, [1], 0, 1, 'euler_improved');

    assert(result.length > 0, 'solve应返回结果');
    const lastPoint = result[result.length - 1];
    assertApproximate(lastPoint.y[0], Math.exp(-1), 0.01, 'solve接口正确性');
});

test('RungeKuttaSolver: 实例化', () => {
    const solver = new RungeKuttaSolver({ stepSize: 0.01 });
    assert(solver.stepSize === 0.01, '步长设置正确');
});

test('RungeKuttaSolver: RK2法', () => {
    const solver = new RungeKuttaSolver({ stepSize: 0.01, adaptiveStep: false });
    const f = (t, y) => [-y[0]];
    const result = solver.rk2(f, 0, [1], 1, 0.01);

    const lastPoint = result[result.length - 1];
    assertApproximate(lastPoint.y[0], Math.exp(-1), 0.001, 'RK2精度');
});

test('RungeKuttaSolver: RK3法', () => {
    const solver = new RungeKuttaSolver({ stepSize: 0.01, adaptiveStep: false });
    const f = (t, y) => [-y[0]];
    const result = solver.rk3(f, 0, [1], 1, 0.01);

    const lastPoint = result[result.length - 1];
    assertApproximate(lastPoint.y[0], Math.exp(-1), 0.0001, 'RK3精度');
});

test('RungeKuttaSolver: RK4法', () => {
    const solver = new RungeKuttaSolver({ stepSize: 0.01, adaptiveStep: false });
    const f = (t, y) => [-y[0]];
    const result = solver.rk4(f, 0, [1], 1, 0.01);

    const lastPoint = result[result.length - 1];
    assertApproximate(lastPoint.y[0], Math.exp(-1), 1e-8, 'RK4应具有高精度');
});

test('RungeKuttaSolver: RK5法', () => {
    const solver = new RungeKuttaSolver({ stepSize: 0.001, adaptiveStep: false });
    const f = (t, y) => [-y[0]];
    const result = solver.rk5(f, 0, [1], 1, 0.001);

    const lastPoint = result[result.length - 1];
    assertApproximate(lastPoint.y[0], Math.exp(-1), 1e-4, 'RK5应具有合理精度');
});

test('RungeKuttaSolver: RK45自适应', () => {
    const solver = new RungeKuttaSolver({ stepSize: 0.1, tolerance: 1e-10 });
    const f = (t, y) => [-y[0]];
    const result = solver.rk45(f, 0, [1], 1, 0.1);

    const lastPoint = result[result.length - 1];
    assertApproximate(lastPoint.y[0], Math.exp(-1), 1e-8, 'RK45自适应精度');
});

test('RungeKuttaSolver: 简谐振动', () => {
    const solver = new RungeKuttaSolver({ stepSize: 0.01, adaptiveStep: false });
    const f = (t, y) => [y[1], -y[0]];
    const result = solver.rk4(f, 0, [1, 0], 6.283, 0.01);

    const lastPoint = result[result.length - 1];
    assertApproximate(lastPoint.y[0], 1, 0.01, '简谐振动应回到初始位置');
});

test('RungeKuttaSolver: solve接口', () => {
    const parser = new EquationParser();
    const solver = new RungeKuttaSolver({ stepSize: 0.01, adaptiveStep: false });
    const eq = parser.parse('-y');
    const result = solver.solve(eq, [1], 0, 1, 'rk4');

    assert(result.length > 0, 'solve应返回结果');
    const lastPoint = result[result.length - 1];
    assertApproximate(lastPoint.y[0], Math.exp(-1), 1e-8, 'solve接口正确性');
});

test('ErrorController: 实例化', () => {
    const controller = new ErrorController({ tolerance: 1e-8 });
    assert(controller.tolerance === 1e-8, '容差设置正确');
});

test('ErrorController: 绝对误差计算', () => {
    const controller = new ErrorController();
    const errors = controller.calculateAbsoluteError([1.1, 2.2], [1.0, 2.0]);
    assertApproximate(errors[0], 0.1, 1e-10, '第一个误差应为0.1');
    assertApproximate(errors[1], 0.2, 1e-10, '第二个误差应为0.2');
});

test('ErrorController: 相对误差计算', () => {
    const controller = new ErrorController();
    const errors = controller.calculateRelativeError([1.1, 2.2], [1.0, 2.0]);
    assertApproximate(errors[0], 0.1, 1e-10, '第一个相对误差应为0.1');
    assertApproximate(errors[1], 0.1, 1e-10, '第二个相对误差应为0.1');
});

test('ErrorController: 最大误差', () => {
    const controller = new ErrorController();
    const maxError = controller.calculateMaxError([0.1, 0.5, 0.3]);
    assertApproximate(maxError, 0.5, 1e-10, '最大误差应为0.5');
});

test('ErrorController: 误差范数', () => {
    const controller = new ErrorController();
    const norm = controller.calculateErrorNorm([3, 4]);
    assertApproximate(norm, 5, 1e-10, '误差范数应为5');
});

test('ErrorController: 步长调整', () => {
    const controller = new ErrorController({ tolerance: 1e-8 });
    const newStep = controller.calculateStepSizeController(0.01, 1e-6, 4);
    assert(newStep < 0.01, '误差太大时应减小步长');
});

test('ErrorController: 收敛性判断', () => {
    const controller = new ErrorController({ tolerance: 1e-8 });
    assert(controller.isConverged(1e-10, 1e-8), '小误差应收敛');
    assert(!controller.isConverged(0.1, 0.01), '大误差不应收敛');
});

test('ErrorController: 误差趋势计算', () => {
    const controller = new ErrorController();
    const results = [
        { t: 0, y: [0] },
        { t: 1, y: [1] },
        { t: 2, y: [4] }
    ];
    const trend = controller.calculateErrorTrend(results);
    assert(trend.length === results.length, '误差趋势应与结果等长');
    assert(trend[0].maxError === 0, '初始误差应为0');
});

test('ErrorController: Richardson外推', () => {
    const controller = new ErrorController();
    const values = [[[1]], [[1.5]]];
    const result = controller.richardsonExtrapolation(values, 0.1, 4);
    assert(Array.isArray(result), '应返回数组');
});

test('ErrorController: 稳定性判断', () => {
    const controller = new ErrorController();
    assert(controller.isStable(-1, 0.5, 'euler'), '欧拉法稳定区域内');
    assert(!controller.isStable(-5, 0.5, 'euler'), '欧拉法稳定区域外');
    assert(controller.isStable(-5, 0.5, 'rk4'), 'RK4稳定区域内');
});

console.log('\n========================================');
console.log('高阶方程系统测试');
console.log('========================================\n');

test('高阶方程: 二阶线性方程', () => {
    const parser = new EquationParser();
    const solver = new RungeKuttaSolver({ stepSize: 0.01, adaptiveStep: false });
    const system = parser.parseHighOrder('-y', 2);

    const f = (t, y) => system.evaluate(t, y);
    const result = solver.rk4(f, 0, [1, 0], 6.283, 0.01);

    const lastPoint = result[result.length - 1];
    assertApproximate(lastPoint.y[0], 1, 0.02, '二阶方程解应周期性');
});

test('高阶方程: 单摆方程', () => {
    const parser = new EquationParser();
    const solver = new RungeKuttaSolver({ stepSize: 0.01, adaptiveStep: false });
    const system = parser.parseHighOrder('-sin(y1)', 2);

    const f = (t, y) => system.evaluate(t, y);
    const result = solver.rk4(f, 0, [0.1, 0], 20, 0.01);

    assert(result.length > 0, '单摆方程应有解');
    const maxY = Math.max(...result.map(p => Math.abs(p.y[0])));
    assert(maxY < 0.5, '小角度单摆振幅应保持');
});

console.log('\n========================================');
console.log('综合测试 - 对比不同方法');
console.log('========================================\n');

test('方法对比: 精度递增', () => {
    const f = (t, y) => [-y[0]];
    const exact = Math.exp(-1);

    const euler = new EulerSolver({ stepSize: 0.01, adaptiveStep: false });
    const rk4 = new RungeKuttaSolver({ stepSize: 0.01, adaptiveStep: false });

    const eulerResult = euler.euler(f, 0, [1], 1, 0.01);
    const rk4Result = rk4.rk4(f, 0, [1], 1, 0.01);

    const eulerError = Math.abs(eulerResult[eulerResult.length - 1].y[0] - exact);
    const rk4Error = Math.abs(rk4Result[rk4Result.length - 1].y[0] - exact);

    assert(rk4Error < eulerError, 'RK4精度应高于欧拉法');
    assert(rk4Error < 1e-8, 'RK4误差应非常小');
});

test('自适应步长 vs 固定步长', () => {
    const f = (t, y) => [-y[0]];
    const exact = Math.exp(-1);

    const fixedSolver = new RungeKuttaSolver({ stepSize: 0.1, adaptiveStep: false });
    const adaptiveSolver = new RungeKuttaSolver({ stepSize: 0.1, tolerance: 1e-10 });

    const fixedResult = fixedSolver.rk4(f, 0, [1], 1, 0.1);
    const adaptiveResult = adaptiveSolver.rk45(f, 0, [1], 1, 0.1);

    const fixedError = Math.abs(fixedResult[fixedResult.length - 1].y[0] - exact);
    const adaptiveError = Math.abs(adaptiveResult[adaptiveResult.length - 1].y[0] - exact);

    assert(adaptiveError < fixedError, '自适应步长精度应更高');
});

console.log('\n========================================');
console.log('边界条件测试');
console.log('========================================\n');

test('边界: 零解', () => {
    const solver = new RungeKuttaSolver({ stepSize: 0.01, adaptiveStep: false });
    const f = (t, y) => [-y[0]];
    const result = solver.rk4(f, 0, [0], 1, 0.01);

    result.forEach(p => {
        assertApproximate(p.y[0], 0, 1e-10, '零解应保持为零');
    });
});

test('边界: 常函数解', () => {
    const solver = new RungeKuttaSolver({ stepSize: 0.01, adaptiveStep: false });
    const f = (t, y) => [0];
    const result = solver.rk4(f, 0, [5], 10, 0.01);

    result.forEach(p => {
        assertApproximate(p.y[0], 5, 1e-10, '常函数解应保持不变');
    });
});

test('边界: 长时间积分', () => {
    const solver = new RungeKuttaSolver({ stepSize: 0.1, adaptiveStep: false });
    const f = (t, y) => [y[1], -y[0]];
    const result = solver.rk4(f, 0, [1, 0], 100, 0.1);

    const energy0 = result[0].y[0] ** 2 + result[0].y[1] ** 2;
    const energyF = result[result.length - 1].y[0] ** 2 + result[result.length - 1].y[1] ** 2;

    assertApproximate(energyF, energy0, 0.1, '能量应近似守恒');
});

console.log('\n========================================');
console.log(`测试完成: ${passed} 通过, ${failed} 失败`);
console.log('========================================');

if (failed > 0) {
    console.log('\n失败的测试:');
    results.filter(r => r.status === 'FAIL').forEach(r => {
        console.log(`  - ${r.name}: ${r.error}`);
    });
}

process.exit(failed > 0 ? 1 : 0);
