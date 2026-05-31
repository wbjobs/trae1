var PerformanceGrade = {
  A: 'A',
  B: 'B',
  C: 'C',
  D: 'D',
  F: 'F'
};

class PerformanceScorer {
  constructor(config) {
    this.config = Object.assign({
      weights: {
        fp: 0.10,
        fcp: 0.15,
        lcp: 0.25,
        ttfb: 0.15,
        dom_ready: 0.15,
        load_time: 0.20
      },
      benchmarks: {
        fp: { good: 1800, poor: 3000 },
        fcp: { good: 1800, poor: 3000 },
        lcp: { good: 2500, poor: 4000 },
        ttfb: { good: 800, poor: 1800 },
        dom_ready: { good: 3000, poor: 5000 },
        load_time: { good: 3000, poor: 5000 }
      }
    }, config || {});

    this._history = [];
    this._maxHistory = 100;
  }

  calculateScore(metrics) {
    var scores = {};
    var totalWeight = 0;
    var weightedScore = 0;

    var metricNames = ['fp', 'fcp', 'lcp', 'ttfb', 'dom_ready', 'load_time'];

    for (var i = 0; i < metricNames.length; i++) {
      var name = metricNames[i];
      var value = metrics[name];

      if (value !== undefined && value !== null && !isNaN(value)) {
        var metricScore = this._scoreMetric(name, value);
        var weight = this.config.weights[name] || 0;

        scores[name] = {
          value: Math.round(value),
          score: metricScore,
          weight: weight,
          grade: this._getGrade(metricScore)
        };

        weightedScore += metricScore * weight;
        totalWeight += weight;
      }
    }

    if (totalWeight === 0) {
      return {
        overall: 0,
        grade: PerformanceGrade.F,
        metrics: scores,
        details: null
      };
    }

    var finalScore = Math.round(weightedScore / totalWeight * 100) / 100;
    var finalGrade = this._getGrade(finalScore);

    var details = this._generateDetails(scores, finalScore, finalGrade);

    this._history.push({
      timestamp: new Date().toISOString(),
      score: finalScore,
      grade: finalGrade,
      metrics: scores
    });

    if (this._history.length > this._maxHistory) {
      this._history.shift();
    }

    return {
      overall: finalScore,
      grade: finalGrade,
      metrics: scores,
      details: details
    };
  }

  _scoreMetric(name, value) {
    var benchmark = this.config.benchmarks[name];
    if (!benchmark) return 50;

    var good = benchmark.good;
    var poor = benchmark.poor;

    if (value <= good) {
      return 90 + (1 - (value / good)) * 10;
    } else if (value >= poor) {
      return Math.max(0, 50 - ((value - poor) / poor) * 50);
    } else {
      var ratio = (value - good) / (poor - good);
      return 90 - ratio * 40;
    }
  }

  _getGrade(score) {
    if (score >= 90) return PerformanceGrade.A;
    if (score >= 80) return PerformanceGrade.B;
    if (score >= 70) return PerformanceGrade.C;
    if (score >= 60) return PerformanceGrade.D;
    return PerformanceGrade.F;
  }

  _generateDetails(scores, overallScore, grade) {
    var strengths = [];
    var weaknesses = [];
    var suggestions = [];

    var metricLabels = {
      fp: '首次绘制',
      fcp: '首次内容绘制',
      lcp: '最大内容绘制',
      ttfb: '首字节时间',
      dom_ready: 'DOM就绪时间',
      load_time: '页面加载时间'
    };

    for (var name in scores) {
      if (scores.hasOwnProperty(name)) {
        var metric = scores[name];
        var label = metricLabels[name] || name;

        if (metric.score >= 85) {
          strengths.push(label);
        } else if (metric.score < 70) {
          weaknesses.push(label);
          suggestions.push(this._getSuggestion(name, metric.value));
        }
      }
    }

    var summary = '';
    if (overallScore >= 90) {
      summary = '页面性能优秀，用户体验良好！';
    } else if (overallScore >= 80) {
      summary = '页面性能良好，仍有优化空间。';
    } else if (overallScore >= 70) {
      summary = '页面性能一般，建议进行优化。';
    } else if (overallScore >= 60) {
      summary = '页面性能较差，需要重点优化。';
    } else {
      summary = '页面性能很差，严重影响用户体验！';
    }

    return {
      summary: summary,
      strengths: strengths,
      weaknesses: weaknesses,
      suggestions: suggestions
    };
  }

  _getSuggestion(metric, value) {
    var suggestions = {
      fp: '建议优化关键渲染路径，减少首次绘制时间',
      fcp: '建议减少首屏内容的资源加载量',
      lcp: '建议优化最大内容元素的加载速度，可使用懒加载或CDN',
      ttfb: '建议优化服务器响应时间，考虑使用缓存或CDN',
      dom_ready: '建议减少DOM节点数量，优化JavaScript执行时间',
      load_time: '建议优化资源加载策略，使用懒加载、代码分割等技术'
    };

    return suggestions[metric] || '建议优化该指标';
  }

  getHistory() {
    return this._history.slice();
  }

  getAverageScore() {
    if (this._history.length === 0) return 0;

    var sum = 0;
    for (var i = 0; i < this._history.length; i++) {
      sum += this._history[i].score;
    }
    return Math.round(sum / this._history.length * 100) / 100;
  }

  getScoreTrend() {
    if (this._history.length < 2) {
      return { trend: 'insufficient', change: 0 };
    }

    var recent = this._history.slice(-5);
    var firstHalf = recent.slice(0, Math.floor(recent.length / 2));
    var secondHalf = recent.slice(Math.floor(recent.length / 2));

    var avgFirst = firstHalf.reduce(function(sum, h) { return sum + h.score; }, 0) / firstHalf.length;
    var avgSecond = secondHalf.reduce(function(sum, h) { return sum + h.score; }, 0) / secondHalf.length;

    var change = Math.round((avgSecond - avgFirst) * 100) / 100;

    if (change > 2) return { trend: 'improving', change: change };
    if (change < -2) return { trend: 'declining', change: change };
    return { trend: 'stable', change: change };
  }

  clearHistory() {
    this._history = [];
  }

  static getGradeColor(grade) {
    var colors = {
      'A': '#4caf50',
      'B': '#8bc34a',
      'C': '#ffeb3b',
      'D': '#ff9800',
      'F': '#f44336'
    };
    return colors[grade] || '#9e9e9e';
  }

  static getGradeLabel(grade) {
    var labels = {
      'A': '优秀',
      'B': '良好',
      'C': '一般',
      'D': '较差',
      'F': '很差'
    };
    return labels[grade] || '未知';
  }
}

if (typeof window !== 'undefined') {
  window.PerformanceScorer = PerformanceScorer;
  window.PerformanceGrade = PerformanceGrade;
}

if (typeof module !== 'undefined' && module.exports) {
  module.exports = { PerformanceScorer: PerformanceScorer, PerformanceGrade: PerformanceGrade };
}
