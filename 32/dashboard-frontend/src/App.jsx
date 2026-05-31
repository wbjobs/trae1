import React, { useEffect, useState, useMemo } from 'react';
import ReactECharts from 'echarts-for-react';
import StrategyManager from './StrategyManager.jsx';

const API_BASE = (import.meta.env.VITE_API_BASE || '');

const fetchMetrics = async (window) => {
  const res = await fetch(`${API_BASE}/api/metrics?window=${window}`);
  if (!res.ok) throw new Error(`HTTP ${res.status}`);
  return res.json();
};

const WindowSelect = ({ value, onChange }) => {
  const options = [5, 15, 30, 60, 180];
  return (
    <div className="window-select">
      {options.map((w) => (
        <button
          key={w}
          className={w === value ? 'active' : ''}
          onClick={() => onChange(w)}
        >
          {w}m
        </button>
      ))}
    </div>
  );
};

const StatCard = ({ label, value, className }) => (
  <div className={`stat-card ${className}`}>
    <div className="label">{label}</div>
    <div className="value">{value}</div>
  </div>
);

const App = () => {
  const [window, setWindow] = useState(60);
  const [data, setData] = useState(null);
  const [error, setError] = useState(null);
  const [loading, setLoading] = useState(true);

  useEffect(() => {
    let cancelled = false;
    const load = async () => {
      try {
        setLoading(true);
        const d = await fetchMetrics(window);
        if (!cancelled) {
          setData(d);
          setError(null);
        }
      } catch (e) {
        if (!cancelled) setError(e.message);
      } finally {
        if (!cancelled) setLoading(false);
      }
    };
    load();
    const id = setInterval(load, 10000);
    return () => {
      cancelled = true;
      clearInterval(id);
    };
  }, [window]);

  const requestChartOpt = useMemo(() => {
    if (!data) return {};
    const times = data.per_minute.map((p) => p.time);
    return {
      backgroundColor: 'transparent',
      tooltip: { trigger: 'axis', axisPointer: { type: 'shadow' } },
      legend: {
        data: ['Pass', 'Verify', 'Reject'],
        textStyle: { color: '#8899a6' },
        top: 0,
      },
      grid: { left: 50, right: 20, top: 40, bottom: 30 },
      xAxis: {
        type: 'category',
        data: times,
        axisLine: { lineStyle: { color: '#2f3336' } },
        axisLabel: { color: '#8899a6' },
      },
      yAxis: {
        type: 'value',
        axisLine: { lineStyle: { color: '#2f3336' } },
        splitLine: { lineStyle: { color: '#2f3336' } },
        axisLabel: { color: '#8899a6' },
      },
      series: [
        {
          name: 'Pass',
          type: 'bar',
          stack: 'total',
          data: data.per_minute.map((p) => p.pass),
          itemStyle: { color: '#17bf63' },
        },
        {
          name: 'Verify',
          type: 'bar',
          stack: 'total',
          data: data.per_minute.map((p) => p.verify),
          itemStyle: { color: '#ffad1f' },
        },
        {
          name: 'Reject',
          type: 'bar',
          stack: 'total',
          data: data.per_minute.map((p) => p.reject),
          itemStyle: { color: '#f4212e' },
        },
      ],
    };
  }, [data]);

  const rejectRateOpt = useMemo(() => {
    if (!data) return {};
    return {
      backgroundColor: 'transparent',
      tooltip: { trigger: 'axis', valueFormatter: (v) => `${v}%` },
      grid: { left: 50, right: 20, top: 30, bottom: 30 },
      xAxis: {
        type: 'category',
        data: data.per_minute.map((p) => p.time),
        axisLine: { lineStyle: { color: '#2f3336' } },
        axisLabel: { color: '#8899a6' },
      },
      yAxis: {
        type: 'value',
        axisLine: { lineStyle: { color: '#2f3336' } },
        splitLine: { lineStyle: { color: '#2f3336' } },
        axisLabel: { color: '#8899a6', formatter: '{value}%' },
        max: 100,
      },
      series: [
        {
          name: 'Reject Rate',
          type: 'line',
          smooth: true,
          data: data.per_minute.map((p) => p.rejectRate),
          itemStyle: { color: '#f4212e' },
          areaStyle: { color: 'rgba(244, 33, 46, 0.2)' },
        },
      ],
    };
  }, [data]);

  const strategyPieOpt = useMemo(() => {
    if (!data) return {};
    const dataArr = data.strategies.map((s) => ({
      name: s.strategy,
      value: s.count,
    }));
    return {
      backgroundColor: 'transparent',
      tooltip: { trigger: 'item', formatter: '{b}: {c} ({d}%)' },
      legend: {
        orient: 'vertical',
        right: 10,
        top: 'center',
        textStyle: { color: '#8899a6' },
      },
      series: [
        {
          name: 'Strategy',
          type: 'pie',
          radius: ['45%', '70%'],
          center: ['35%', '50%'],
          avoidLabelOverlap: true,
          itemStyle: {
            borderRadius: 4,
            borderColor: '#15181c',
            borderWidth: 2,
          },
          label: { show: false },
          data: dataArr,
          color: ['#1d9bf0', '#f4212e', '#ffad1f', '#17bf63', '#8899a6', '#8b5cf6'],
        },
      ],
    };
  }, [data]);

  const summary = data?.summary || { total: 0, pass: 0, reject: 0, verify: 0, rejectRate: 0 };

  const [view, setView] = useState('dashboard');

  return (
    <div className="app">
      <div className="header">
        <div>
          <h1>🛡️ Risk Control Dashboard</h1>
          <div className="meta">
            Last update: {data ? new Date(data.generated_at).toLocaleTimeString() : '—'}
            {error ? ` · error: ${error}` : ''}
          </div>
        </div>
        <div style={{ display: 'flex', gap: 16, alignItems: 'center' }}>
          {view === 'dashboard' && <WindowSelect value={window} onChange={setWindow} />}
          <div className="window-select">
            <button
              className={view === 'dashboard' ? 'active' : ''}
              onClick={() => setView('dashboard')}
            >
              监控面板
            </button>
            <button
              className={view === 'strategy' ? 'active' : ''}
              onClick={() => setView('strategy')}
            >
              策略管理
            </button>
          </div>
        </div>
      </div>

      {view === 'strategy' ? (
        <StrategyManager />
      ) : loading && !data ? (
        <div className="loading">Loading metrics…</div>
      ) : (
        <>
          <div className="stats">
            <StatCard className="total" label="Total Requests" value={summary.total.toLocaleString()} />
            <StatCard className="pass" label="Passed" value={summary.pass.toLocaleString()} />
            <StatCard className="verify" label="Verified" value={summary.verify.toLocaleString()} />
            <StatCard className="reject" label="Rejected" value={`${summary.reject.toLocaleString()} (${summary.rejectRate}%)`} />
          </div>

          <div className="chart-grid">
            <div className="chart-card">
              <h3>Requests per minute</h3>
              <div className="chart">
                <ReactECharts option={requestChartOpt} style={{ height: '100%', width: '100%' }} theme="dark" />
              </div>
            </div>
            <div className="chart-card">
              <h3>Strategy hit distribution</h3>
              <div className="chart">
                <ReactECharts option={strategyPieOpt} style={{ height: '100%', width: '100%' }} theme="dark" />
              </div>
            </div>
          </div>

          <div className="chart-card full-row">
            <h3>Reject rate (%)</h3>
            <div className="chart">
              <ReactECharts option={rejectRateOpt} style={{ height: '100%', width: '100%' }} theme="dark" />
            </div>
          </div>
        </>
      )}
    </div>
  );
};

export default App;
