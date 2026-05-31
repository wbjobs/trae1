class Renderer {
    constructor(canvasId) {
        this.canvas = document.getElementById(canvasId);
        if (!this.canvas) {
            throw new Error(`Canvas ${canvasId} 未找到`);
        }
        this.ctx = this.canvas.getContext('2d');
        this.width = this.canvas.width;
        this.height = this.canvas.height;
        this.padding = { top: 40, right: 40, bottom: 50, left: 70 };
        this.plotArea = {
            x: this.padding.left,
            y: this.padding.top,
            width: this.width - this.padding.left - this.padding.right,
            height: this.height - this.padding.top - this.padding.bottom
        };
    }

    clear() {
        this.ctx.clearRect(0, 0, this.width, this.height);
    }

    setSize(width, height) {
        this.width = width;
        this.height = height;
        this.canvas.width = width;
        this.canvas.height = height;
        this.plotArea = {
            x: this.padding.left,
            y: this.padding.top,
            width: this.width - this.padding.left - this.padding.right,
            height: this.height - this.padding.top - this.padding.bottom
        };
    }

    calculateDataRange(data, yIndex = 0) {
        let minT = Infinity, maxT = -Infinity;
        let minY = Infinity, maxY = -Infinity;

        data.forEach(point => {
            minT = Math.min(minT, point.t);
            maxT = Math.max(maxT, point.t);
            const y = point.y[yIndex];
            minY = Math.min(minY, y);
            maxY = Math.max(maxY, y);
        });

        return { minT, maxT, minY, maxY };
    }

    calculateNiceRange(min, max, targetTicks = 10) {
        const range = max - min;
        const roughTickSize = range / targetTicks;
        const magnitude = Math.pow(10, Math.floor(Math.log10(roughTickSize)));
        const normalized = roughTickSize / magnitude;

        let tickSize;
        if (normalized <= 1.5) tickSize = 1 * magnitude;
        else if (normalized <= 3) tickSize = 2 * magnitude;
        else if (normalized <= 7) tickSize = 5 * magnitude;
        else tickSize = 10 * magnitude;

        const niceMin = Math.floor(min / tickSize) * tickSize;
        const niceMax = Math.ceil(max / tickSize) * tickSize;

        return { min: niceMin, max: niceMax, tickSize };
    }

    mapToScreen(t, y, tRange, yRange) {
        const x = this.plotArea.x + (t - tRange.min) / (tRange.max - tRange.min) * this.plotArea.width;
        const yScreen = this.plotArea.y + this.plotArea.height - (y - yRange.min) / (yRange.max - yRange.min) * this.plotArea.height;
        return { x, y: yScreen };
    }

    drawAxes(tRange, yRange, title = '', xLabel = 't', yLabel = 'y(t)') {
        const ctx = this.ctx;
        const { x, y, width, height } = this.plotArea;

        ctx.strokeStyle = '#333';
        ctx.lineWidth = 2;
        ctx.beginPath();
        ctx.moveTo(x, y);
        ctx.lineTo(x, y + height);
        ctx.lineTo(x + width, y + height);
        ctx.stroke();

        ctx.fillStyle = '#333';
        ctx.font = '12px Arial';
        ctx.textAlign = 'center';
        ctx.textBaseline = 'top';

        const xTickRange = this.calculateNiceRange(tRange.min, tRange.max, 10);
        const xTicks = Math.round((xTickRange.max - xTickRange.min) / xTickRange.tickSize);

        for (let i = 0; i <= xTicks; i++) {
            const value = xTickRange.min + i * xTickRange.tickSize;
            const screenX = this.mapToScreen(value, 0, xTickRange, yRange).x;

            ctx.strokeStyle = '#ddd';
            ctx.lineWidth = 1;
            ctx.beginPath();
            ctx.moveTo(screenX, y);
            ctx.lineTo(screenX, y + height);
            ctx.stroke();

            ctx.fillStyle = '#666';
            ctx.fillText(value.toFixed(2), screenX, y + height + 5);
        }

        ctx.textAlign = 'right';
        ctx.textBaseline = 'middle';

        const yTickRange = this.calculateNiceRange(yRange.min, yRange.max, 8);
        const yTicks = Math.round((yTickRange.max - yTickRange.min) / yTickRange.tickSize);

        for (let i = 0; i <= yTicks; i++) {
            const value = yTickRange.min + i * yTickRange.tickSize;
            const screenY = this.mapToScreen(0, value, xTickRange, yTickRange).y;

            ctx.strokeStyle = '#ddd';
            ctx.lineWidth = 1;
            ctx.beginPath();
            ctx.moveTo(x, screenY);
            ctx.lineTo(x + width, screenY);
            ctx.stroke();

            ctx.fillStyle = '#666';
            ctx.fillText(value.toFixed(2), x - 5, screenY);
        }

        if (title) {
            ctx.fillStyle = '#333';
            ctx.font = 'bold 16px Arial';
            ctx.textAlign = 'center';
            ctx.textBaseline = 'top';
            ctx.fillText(title, x + width / 2, 10);
        }

        ctx.fillStyle = '#333';
        ctx.font = '14px Arial';
        ctx.textAlign = 'center';
        ctx.textBaseline = 'bottom';
        ctx.fillText(xLabel, x + width / 2, this.height - 10);

        ctx.save();
        ctx.translate(15, y + height / 2);
        ctx.rotate(-Math.PI / 2);
        ctx.textAlign = 'center';
        ctx.textBaseline = 'middle';
        ctx.fillText(yLabel, 0, 0);
        ctx.restore();

        return { tRange: xTickRange, yRange: yTickRange };
    }

    drawCurve(data, tRange, yRange, options = {}) {
        const ctx = this.ctx;
        const color = options.color || '#2196F3';
        const lineWidth = options.lineWidth || 2;
        const yIndex = options.yIndex || 0;

        ctx.strokeStyle = color;
        ctx.lineWidth = lineWidth;
        ctx.beginPath();

        data.forEach((point, i) => {
            const y = point.y[yIndex];
            const screen = this.mapToScreen(point.t, y, tRange, yRange);

            if (i === 0) {
                ctx.moveTo(screen.x, screen.y);
            } else {
                ctx.lineTo(screen.x, screen.y);
            }
        });

        ctx.stroke();
    }

    drawPoints(data, tRange, yRange, options = {}) {
        const ctx = this.ctx;
        const color = options.color || '#F44336';
        const size = options.size || 4;
        const yIndex = options.yIndex || 0;

        ctx.fillStyle = color;

        data.forEach((point, i) => {
            const y = point.y[yIndex];
            const screen = this.mapToScreen(point.t, y, tRange, yRange);

            ctx.beginPath();
            ctx.arc(screen.x, screen.y, size, 0, Math.PI * 2);
            ctx.fill();
        });
    }

    drawLegend(curves, position = 'top') {
        const ctx = this.ctx;
        const { x, y, width } = this.plotArea;

        ctx.font = '12px Arial';
        ctx.textAlign = 'left';
        ctx.textBaseline = 'middle';

        const legendWidth = curves.reduce((sum, c) => sum + ctx.measureText(c.label).width + 30, 0);
        let startX = x + (width - legendWidth) / 2;
        const legendY = y - 20;

        curves.forEach((curve, i) => {
            ctx.fillStyle = curve.color || '#2196F3';
            ctx.fillRect(startX, legendY - 6, 16, 12);

            ctx.fillStyle = '#333';
            ctx.fillText(curve.label, startX + 22, legendY);

            startX += ctx.measureText(curve.label).width + 30;
        });
    }

    drawGrid(tRange, yRange) {
        const ctx = this.ctx;
        const { x, y, width, height } = this.plotArea;

        ctx.strokeStyle = '#f0f0f0';
        ctx.lineWidth = 1;

        const xTicks = this.calculateNiceRange(tRange.min, tRange.max, 10);
        const xTickCount = Math.round((xTicks.max - xTicks.min) / xTicks.tickSize);

        for (let i = 0; i <= xTickCount; i++) {
            const value = xTicks.min + i * xTicks.tickSize;
            const screenX = this.mapToScreen(value, 0, xTicks, yRange).x;

            ctx.beginPath();
            ctx.moveTo(screenX, y);
            ctx.lineTo(screenX, y + height);
            ctx.stroke();
        }

        const yTicks = this.calculateNiceRange(yRange.min, yRange.max, 8);
        const yTickCount = Math.round((yTicks.max - yTicks.min) / yTicks.tickSize);

        for (let i = 0; i <= yTickCount; i++) {
            const value = yTicks.min + i * yTicks.tickSize;
            const screenY = this.mapToScreen(0, value, xTicks, yTicks).y;

            ctx.beginPath();
            ctx.moveTo(x, screenY);
            ctx.lineTo(x + width, screenY);
            ctx.stroke();
        }
    }

    renderSolution(data, options = {}) {
        this.clear();

        const yIndex = options.yIndex || 0;
        const title = options.title || '数值解曲线';
        const xLabel = options.xLabel || 't';
        const yLabel = options.yLabel || 'y(t)';

        const range = this.calculateDataRange(data, yIndex);
        const tRange = this.calculateNiceRange(range.minT, range.maxT, 10);
        const yRange = this.calculateNiceRange(range.minY, range.maxY, 8);

        this.drawGrid(tRange, yRange);

        const finalRanges = this.drawAxes(tRange, yRange, title, xLabel, yLabel);

        this.drawCurve(data, finalRanges.tRange, finalRanges.yRange, {
            color: options.color || '#2196F3',
            lineWidth: options.lineWidth || 2,
            yIndex
        });

        if (options.points) {
            this.drawPoints(data, finalRanges.tRange, finalRanges.yRange, {
                color: options.pointColor || '#F44336',
                size: options.pointSize || 3,
                yIndex
            });
        }

        return { tRange: finalRanges.tRange, yRange: finalRanges.yRange };
    }

    renderMultipleSolutions(datasets, options = {}) {
        this.clear();

        const yIndex = options.yIndex || 0;
        const title = options.title || '多种方法对比';
        const xLabel = options.xLabel || 't';
        const yLabel = options.yLabel || 'y(t)';

        let minT = Infinity, maxT = -Infinity;
        let minY = Infinity, maxY = -Infinity;

        datasets.forEach(ds => {
            const range = this.calculateDataRange(ds.data, yIndex);
            minT = Math.min(minT, range.minT);
            maxT = Math.max(maxT, range.maxT);
            minY = Math.min(minY, range.minY);
            maxY = Math.max(maxY, range.maxY);
        });

        const tRange = this.calculateNiceRange(minT, maxT, 10);
        const yRange = this.calculateNiceRange(minY, maxY, 8);

        this.drawGrid(tRange, yRange);
        const finalRanges = this.drawAxes(tRange, yRange, title, xLabel, yLabel);

        datasets.forEach((ds, i) => {
            this.drawCurve(ds.data, finalRanges.tRange, finalRanges.yRange, {
                color: ds.color || this.getDefaultColor(i),
                lineWidth: ds.lineWidth || 2,
                yIndex
            });
        });

        this.drawLegend(datasets.map((ds, i) => ({
            label: ds.label || `方法 ${i + 1}`,
            color: ds.color || this.getDefaultColor(i)
        })));

        return { tRange: finalRanges.tRange, yRange: finalRanges.yRange };
    }

    renderErrorTrend(errorData, options = {}) {
        this.clear();

        const title = options.title || '误差趋势图';
        const xLabel = options.xLabel || 't';
        const yLabel = options.yLabel || '误差';

        let minT = Infinity, maxT = -Infinity;
        let maxError = 0;

        errorData.forEach(point => {
            minT = Math.min(minT, point.t);
            maxT = Math.max(maxT, point.t);
            maxError = Math.max(maxError, point.maxError || 0);
        });

        const tRange = this.calculateNiceRange(minT, maxT, 10);
        const yRange = this.calculateNiceRange(0, maxError, 8);

        this.drawGrid(tRange, yRange);
        const finalRanges = this.drawAxes(tRange, yRange, title, xLabel, yLabel);

        const ctx = this.ctx;
        ctx.strokeStyle = options.color || '#FF5722';
        ctx.lineWidth = 2;
        ctx.beginPath();

        errorData.forEach((point, i) => {
            const screen = this.mapToScreen(point.t, point.maxError || 0, finalRanges.tRange, finalRanges.yRange);

            if (i === 0) {
                ctx.moveTo(screen.x, screen.y);
            } else {
                ctx.lineTo(screen.x, screen.y);
            }
        });

        ctx.stroke();

        ctx.fillStyle = '#4CAF50';
        errorData.forEach(point => {
            const screen = this.mapToScreen(point.t, point.maxError || 0, finalRanges.tRange, finalRanges.yRange);
            ctx.beginPath();
            ctx.arc(screen.x, screen.y, 3, 0, Math.PI * 2);
            ctx.fill();
        });

        return { tRange: finalRanges.tRange, yRange: finalRanges.yRange };
    }

    renderPhasePortrait(data, options = {}) {
        this.clear();

        const xIndex = options.xIndex || 0;
        const yIndex = options.yIndex || 1;
        const title = options.title || '相图';
        const xLabel = options.xLabel || `y${xIndex + 1}`;
        const yLabel = options.yLabel || `y${yIndex + 1}`;

        let minX = Infinity, maxX = -Infinity;
        let minY = Infinity, maxY = -Infinity;

        data.forEach(point => {
            const x = point.y[xIndex];
            const y = point.y[yIndex];
            minX = Math.min(minX, x);
            maxX = Math.max(maxX, x);
            minY = Math.min(minY, y);
            maxY = Math.max(maxY, y);
        });

        const xRange = this.calculateNiceRange(minX, maxX, 10);
        const yRange = this.calculateNiceRange(minY, maxY, 8);

        this.drawGrid(xRange, yRange);

        const ctx = this.ctx;
        const { x, y, width, height } = this.plotArea;

        ctx.strokeStyle = '#333';
        ctx.lineWidth = 2;
        ctx.beginPath();
        ctx.moveTo(x, y);
        ctx.lineTo(x, y + height);
        ctx.lineTo(x + width, y + height);
        ctx.stroke();

        ctx.fillStyle = '#333';
        ctx.font = '12px Arial';
        ctx.textAlign = 'center';
        ctx.textBaseline = 'top';

        const xTicks = this.calculateNiceRange(xRange.min, xRange.max, 10);
        const xTickCount = Math.round((xTicks.max - xTicks.min) / xTicks.tickSize);

        for (let i = 0; i <= xTickCount; i++) {
            const value = xTicks.min + i * xTicks.tickSize;
            const screenX = x + (value - xTicks.min) / (xTicks.max - xTicks.min) * width;
            ctx.fillText(value.toFixed(2), screenX, y + height + 5);
        }

        ctx.textAlign = 'right';
        ctx.textBaseline = 'middle';

        const yTicks = this.calculateNiceRange(yRange.min, yRange.max, 8);
        const yTickCount = Math.round((yTicks.max - yTicks.min) / yTicks.tickSize);

        for (let i = 0; i <= yTickCount; i++) {
            const value = yTicks.min + i * yTicks.tickSize;
            const screenY = y + height - (value - yTicks.min) / (yTicks.max - yTicks.min) * height;
            ctx.fillText(value.toFixed(2), x - 5, screenY);
        }

        if (title) {
            ctx.fillStyle = '#333';
            ctx.font = 'bold 16px Arial';
            ctx.textAlign = 'center';
            ctx.textBaseline = 'top';
            ctx.fillText(title, x + width / 2, 10);
        }

        ctx.fillStyle = '#333';
        ctx.font = '14px Arial';
        ctx.textAlign = 'center';
        ctx.textBaseline = 'bottom';
        ctx.fillText(xLabel, x + width / 2, this.height - 10);

        ctx.save();
        ctx.translate(15, y + height / 2);
        ctx.rotate(-Math.PI / 2);
        ctx.textAlign = 'center';
        ctx.textBaseline = 'middle';
        ctx.fillText(yLabel, 0, 0);
        ctx.restore();

        ctx.strokeStyle = options.color || '#9C27B0';
        ctx.lineWidth = 2;
        ctx.beginPath();

        data.forEach((point, i) => {
            const xVal = point.y[xIndex];
            const yVal = point.y[yIndex];

            const screenX = x + (xVal - xTicks.min) / (xTicks.max - xTicks.min) * width;
            const screenY = y + height - (yVal - yTicks.min) / (yTicks.max - yTicks.min) * height;

            if (i === 0) {
                ctx.moveTo(screenX, screenY);
            } else {
                ctx.lineTo(screenX, screenY);
            }
        });

        ctx.stroke();

        return { xRange: xTicks, yRange: yTicks };
    }

    getDefaultColor(index) {
        const colors = [
            '#2196F3', '#F44336', '#4CAF50', '#FF9800', '#9C27B0',
            '#00BCD4', '#795548', '#E91E63', '#3F51B5', '#8BC34A'
        ];
        return colors[index % colors.length];
    }

    renderErrorAnalysis(analysis, options = {}) {
        this.clear();

        const ctx = this.ctx;
        const { x, y, width, height } = this.plotArea;

        const title = options.title || '误差溯源分析';
        ctx.fillStyle = '#333';
        ctx.font = 'bold 16px Arial';
        ctx.textAlign = 'center';
        ctx.textBaseline = 'top';
        ctx.fillText(title, x + width / 2, 10);

        const sources = analysis.errorSources || {};
        const sourceLabels = [];
        const sourceValues = [];

        for (const [key, source] of Object.entries(sources)) {
            sourceLabels.push(source.description);
            sourceValues.push(source.percentage);
        }

        const barArea = {
            x: x + 20,
            y: y + 40,
            width: width - 40,
            height: height - 100
        };

        const barHeight = 30;
        const barSpacing = 20;
        const totalBarHeight = sourceLabels.length * (barHeight + barSpacing);
        const startY = barArea.y + (barArea.height - totalBarHeight) / 2;

        sourceLabels.forEach((label, i) => {
            const barY = startY + i * (barHeight + barSpacing);
            const percentage = sourceValues[i];
            const barWidth = (percentage / 100) * barArea.width * 0.8;

            ctx.fillStyle = this.getErrorColor(percentage);
            ctx.fillRect(barArea.x + 80, barY, barWidth, barHeight);

            ctx.strokeStyle = '#333';
            ctx.lineWidth = 1;
            ctx.strokeRect(barArea.x + 80, barY, barArea.width * 0.8, barHeight);

            ctx.fillStyle = '#333';
            ctx.font = '12px Arial';
            ctx.textAlign = 'right';
            ctx.textBaseline = 'middle';
            ctx.fillText(label, barArea.x + 70, barY + barHeight / 2);

            ctx.textAlign = 'left';
            ctx.fillText(`${percentage.toFixed(1)}%`, barArea.x + 80 + barWidth + 10, barY + barHeight / 2);
        });

        ctx.font = '12px Arial';
        ctx.textAlign = 'left';
        ctx.fillStyle = '#666';
        const infoY = startY + totalBarHeight + 10;

        ctx.fillText(`总误差: ${analysis.totalError?.toExponential(4) || 'N/A'}`, barArea.x, infoY);
        ctx.fillText(`可靠性: ${analysis.reliability || 'N/A'}`, barArea.x, infoY + 20);

        if (analysis.recommendations && analysis.recommendations.length > 0) {
            ctx.fillStyle = '#333';
            ctx.font = 'bold 12px Arial';
            ctx.fillText('优化建议:', barArea.x, infoY + 50);

            analysis.recommendations.slice(0, 3).forEach((rec, i) => {
                ctx.font = '11px Arial';
                ctx.fillStyle = this.getSeverityColor(rec.severity);
                ctx.fillText(`• ${rec.message}`, barArea.x, infoY + 70 + i * 20);
            });
        }

        return { startY, barArea };
    }

    renderClassificationInfo(classification, options = {}) {
        const ctx = this.ctx;
        const x = options.x || 10;
        const y = options.y || 10;

        ctx.fillStyle = 'rgba(255, 255, 255, 0.9)';
        ctx.fillRect(x, y, 280, 180);

        ctx.strokeStyle = '#ddd';
        ctx.lineWidth = 1;
        ctx.strokeRect(x, y, 280, 180);

        ctx.fillStyle = '#333';
        ctx.font = 'bold 14px Arial';
        ctx.textAlign = 'left';
        ctx.textBaseline = 'top';

        const typeNames = {
            'general': '一般微分方程',
            'stiff': '刚性微分方程',
            'oscillatory': '振荡型微分方程',
            'exponential': '指数型微分方程',
            'linear': '线性微分方程'
        };

        ctx.fillText('方程类型分析:', x + 10, y + 10);

        ctx.font = '12px Arial';
        let offsetY = y + 35;

        ctx.fillStyle = '#2196F3';
        ctx.fillText(`类型: ${typeNames[classification.type] || '未知'}`, x + 10, offsetY);
        offsetY += 20;

        ctx.fillStyle = '#666';
        ctx.fillText(`维度: ${classification.dimension}`, x + 10, offsetY);
        offsetY += 18;

        ctx.fillText(`自治: ${classification.isAutonomous ? '是' : '否'}`, x + 10, offsetY);
        offsetY += 18;

        ctx.fillText(`线性: ${classification.isLinear ? '是' : '否'}`, x + 10, offsetY);
        offsetY += 18;

        if (classification.isStiff) {
            ctx.fillStyle = '#F44336';
        }
        ctx.fillText(`刚性: ${classification.isStiff ? '是' : '否'} (比值: ${classification.stiffnessRatio.toFixed(2)})`, x + 10, offsetY);
        offsetY += 18;

        ctx.fillStyle = '#666';
        ctx.fillText(`振荡: ${classification.isOscillatory ? '是' : '否'}`, x + 10, offsetY);
        offsetY += 18;

        ctx.fillText(`指数: ${classification.isExponential ? '是' : '否'}`, x + 10, offsetY);

        if (classification.recommendedMethods && classification.recommendedMethods.length > 0) {
            offsetY += 25;
            ctx.fillStyle = '#333';
            ctx.font = 'bold 12px Arial';
            ctx.fillText('推荐方法:', x + 10, offsetY);
            offsetY += 18;

            ctx.font = '11px Arial';
            classification.recommendedMethods.slice(0, 3).forEach((method, i) => {
                ctx.fillStyle = this.getMethodColor(i);
                ctx.fillText(`  ${method.method}: ${method.score}分 - ${method.reason}`, x + 10, offsetY);
                offsetY += 16;
            });
        }

        return { width: 280, height: 180 };
    }

    getErrorColor(percentage) {
        if (percentage > 70) return '#F44336';
        if (percentage > 40) return '#FF9800';
        return '#4CAF50';
    }

    getSeverityColor(severity) {
        switch (severity) {
            case 'critical': return '#F44336';
            case 'high': return '#FF9800';
            case 'medium': return '#FFC107';
            default: return '#4CAF50';
        }
    }

    getMethodColor(index) {
        const colors = ['#2196F3', '#4CAF50', '#FF9800'];
        return colors[index % colors.length];
    }
}

if (typeof module !== 'undefined' && module.exports) {
    module.exports = Renderer;
}
