class EquationParser {
    constructor() {
        this.functions = {
            'sin': Math.sin,
            'cos': Math.cos,
            'tan': Math.tan,
            'exp': Math.exp,
            'log': Math.log,
            'ln': Math.log,
            'sqrt': Math.sqrt,
            'abs': Math.abs,
            'pow': Math.pow,
            'asin': Math.asin,
            'acos': Math.acos,
            'atan': Math.atan,
            'sinh': Math.sinh,
            'cosh': Math.cosh,
            'tanh': Math.tanh
        };
        this.constants = {
            'pi': Math.PI,
            'PI': Math.PI,
            'e': Math.E,
            'E': Math.E
        };
    }

    parse(input) {
        if (typeof input === 'function') {
            return {
                type: 'function',
                fn: input,
                evaluate: (t, y) => {
                    if (Array.isArray(y)) {
                        return input(t, ...y);
                    }
                    return input(t, y);
                }
            };
        }

        if (typeof input === 'string') {
            return this.parseString(input);
        }

        throw new Error('不支持的方程格式');
    }

    parseString(expression) {
        const sanitized = this.sanitize(expression);
        const tokens = this.tokenize(sanitized);
        const ast = this.parseTokens(tokens);

        return {
            type: 'expression',
            expression: sanitized,
            ast: ast,
            evaluate: (t, y) => this.evaluateAST(ast, t, y)
        };
    }

    sanitize(expr) {
        return expr
            .replace(/\s+/g, '')
            .replace(/\^/g, '**')
            .replace(/(\d)([a-zA-Z])/g, '$1*$2')
            .replace(/([a-zA-Z])(\d)/g, '$1*$2')
            .replace(/\)(\()/g, ')*(')
            .replace(/(\d)(\()/g, '$1*(')
            .replace(/\)(\d)/g, ')*$1');
    }

    tokenize(expr) {
        const tokens = [];
        let i = 0;

        while (i < expr.length) {
            const ch = expr[i];

            if (ch === '*' && expr[i + 1] === '*') {
                tokens.push({ type: 'operator', value: '**' });
                i += 2;
                continue;
            }

            if (/\d|\./.test(ch)) {
                let num = '';
                while (i < expr.length && /[\d.]/.test(expr[i])) {
                    num += expr[i++];
                }
                tokens.push({ type: 'number', value: parseFloat(num) });
                continue;
            }

            if (/[a-zA-Z_]/.test(ch)) {
                let name = '';
                while (i < expr.length && /[a-zA-Z_0-9]/.test(expr[i])) {
                    name += expr[i++];
                }
                tokens.push({ type: 'identifier', value: name });
                continue;
            }

            if ('+-*/(),'.includes(ch)) {
                tokens.push({ type: 'operator', value: ch });
                i++;
                continue;
            }

            throw new Error(`无法识别的字符: ${ch}`);
        }

        return tokens;
    }

    parseTokens(tokens) {
        let pos = 0;

        const peek = () => tokens[pos];
        const consume = () => tokens[pos++];

        const parseExpression = () => {
            let left = parseTerm();
            while (peek() && (peek().value === '+' || peek().value === '-')) {
                const op = consume();
                const right = parseTerm();
                left = { type: 'binary', op: op.value, left, right };
            }
            return left;
        };

        const parseTerm = () => {
            let left = parsePower();
            while (peek() && (peek().value === '*' || peek().value === '/')) {
                const op = consume();
                const right = parsePower();
                left = { type: 'binary', op: op.value, left, right };
            }
            return left;
        };

        const parsePower = () => {
            let left = parseUnary();
            while (peek() && peek().value === '**') {
                const op = consume();
                const right = parseUnary();
                left = { type: 'binary', op: '**', left, right };
            }
            return left;
        };

        const parseUnary = () => {
            if (peek() && (peek().value === '-' || peek().value === '+')) {
                const op = consume();
                const operand = parseUnary();
                return { type: 'unary', op: op.value, operand };
            }
            return parsePrimary();
        };

        const parsePrimary = () => {
            const token = peek();

            if (!token) {
                throw new Error('表达式意外结束');
            }

            if (token.type === 'number') {
                consume();
                return { type: 'number', value: token.value };
            }

            if (token.type === 'identifier') {
                consume();
                if (peek() && peek().value === '(') {
                    consume();
                    const args = [];
                    if (peek() && peek().value !== ')') {
                        args.push(parseExpression());
                        while (peek() && peek().value === ',') {
                            consume();
                            args.push(parseExpression());
                        }
                    }
                    if (!peek() || peek().value !== ')') {
                        throw new Error('缺少右括号');
                    }
                    consume();
                    return { type: 'function', name: token.value, args };
                }
                return { type: 'variable', name: token.value };
            }

            if (token.value === '(') {
                consume();
                const expr = parseExpression();
                if (!peek() || peek().value !== ')') {
                    throw new Error('缺少右括号');
                }
                consume();
                return expr;
            }

            throw new Error(`意外的标记: ${JSON.stringify(token)}`);
        };

        return parseExpression();
    }

    evaluateAST(node, t, y) {
        switch (node.type) {
            case 'number':
                return node.value;

            case 'variable':
                if (node.name === 't' || node.name === 'x') {
                    return t;
                }
                if (node.name === 'y') {
                    return Array.isArray(y) ? y[0] : y;
                }
                if (node.name.startsWith('y') && node.name.length > 1) {
                    const idx = parseInt(node.name.slice(1)) - 1;
                    if (Array.isArray(y) && idx >= 0 && idx < y.length) {
                        return y[idx];
                    }
                }
                if (this.constants.hasOwnProperty(node.name)) {
                    return this.constants[node.name];
                }
                return 0;

            case 'unary': {
                const val = this.evaluateAST(node.operand, t, y);
                return node.op === '-' ? -val : val;
            }

            case 'binary': {
                const left = this.evaluateAST(node.left, t, y);
                const right = this.evaluateAST(node.right, t, y);
                switch (node.op) {
                    case '+': return left + right;
                    case '-': return left - right;
                    case '*': return left * right;
                    case '/': return left / right;
                    case '**': return Math.pow(left, right);
                }
            }

            case 'function': {
                const args = node.args.map(a => this.evaluateAST(a, t, y));
                if (this.functions.hasOwnProperty(node.name)) {
                    return this.functions[node.name](...args);
                }
                throw new Error(`未知函数: ${node.name}`);
            }
        }

        throw new Error(`无法计算的节点: ${JSON.stringify(node)}`);
    }

    parseSystem(equations) {
        if (!Array.isArray(equations)) {
            equations = [equations];
        }
        return equations.map(eq => this.parse(eq));
    }

    parseHighOrder(expression, order) {
        const eq = this.parse(expression);

        const equations = [];
        for (let i = 0; i < order - 1; i++) {
            equations.push((t, ...y) => y[i + 1] !== undefined ? y[i + 1] : 0);
        }
        equations.push((t, ...y) => eq.evaluate(t, [...y]));

        const system = {
            type: 'system',
            order: order,
            equations: equations,
            evaluate: (t, y) => {
                const yArray = Array.isArray(y) ? y : [y];
                const result = [];

                for (let i = 0; i < order - 1; i++) {
                    result.push(yArray[i + 1] !== undefined ? yArray[i + 1] : 0);
                }

                result.push(eq.evaluate(t, yArray));

                return result;
            }
        };

        return system;
    }
}

if (typeof module !== 'undefined' && module.exports) {
    module.exports = EquationParser;
}
