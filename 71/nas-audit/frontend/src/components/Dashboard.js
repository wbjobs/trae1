import React, { useState, useEffect, useCallback } from 'react';
import ReactECharts from 'echarts-for-react';
import { auditApi } from '../api/client';
import dayjs from 'dayjs';

const Dashboard = () => {
    const [days, setDays] = useState(7);
    const [loading, setLoading] = useState(true);
    const [data, setData] = useState({
        top_users: [],
        operation_trend: [],
        extension_stats: [],
    });
    const [health, setHealth] = useState({
        status: 'unknown',
        elasticsearch: false,
        smb_connected: false,
        uptime_seconds: 0,
    });

    const fetchData = useCallback(async () => {
        setLoading(true);
        try {
            const [dashRes, healthRes] = await Promise.all([
                auditApi.getDashboard(days, 10),
                auditApi.health(),
            ]);
            setData(dashRes.data);
            setHealth(healthRes.data);
            updateStatusIndicator(healthRes.data);
        } catch (error) {
            console.error('Failed to fetch dashboard data:', error);
        } finally {
            setLoading(false);
        }
    }, [days]);

    const updateStatusIndicator = (healthData) => {
        const indicator = document.getElementById('statusIndicator');
        if (!indicator) return;
        const dot = indicator.querySelector('.status-dot');
        const text = indicator.querySelector('.status-text');
        if (healthData.elasticsearch && healthData.smb_connected) {
            dot.className = 'status-dot healthy';
            text.textContent = '服务正常';
        } else if (healthData.elasticsearch) {
            dot.className = 'status-dot degraded';
            text.textContent = 'SMB未连接';
        } else {
            dot.className = 'status-dot offline';
            text.textContent = 'Elasticsearch未连接';
        }
    };

    useEffect(() => {
        fetchData();
        const interval = setInterval(fetchData, 60000);
        return () => clearInterval(interval);
    }, [fetchData]);

    const topUsersOption = {
        tooltip: { trigger: 'axis', axisPointer: { type: 'shadow' } },
        grid: { left: 120, right: 20, top: 20, bottom: 40 },
        xAxis: {
            type: 'value',
            axisLine: { lineStyle: { color: '#3c4752' } },
            axisLabel: { color: '#98a2b3' },
            splitLine: { lineStyle: { color: '#2a333d' } },
        },
        yAxis: {
            type: 'category',
            data: data.top_users.map(u => u.username).reverse(),
            axisLine: { lineStyle: { color: '#3c4752' } },
            axisLabel: { color: '#98a2b3', fontSize: 12 },
        },
        series: [{
            type: 'bar',
            data: data.top_users.map(u => u.count).reverse(),
            barWidth: '60%',
            itemStyle: {
                color: {
                    type: 'linear', x: 0, y: 0, x2: 1, y2: 0,
                    colorStops: [
                        { offset: 0, color: '#00bfb3' },
                        { offset: 1, color: '#00a699' },
                    ],
                },
                borderRadius: [0, 4, 4, 0],
            },
            label: {
                show: true,
                position: 'right',
                color: '#98a2b3',
                fontWeight: 500,
            },
        }],
    };

    const trendOption = {
        tooltip: {
            trigger: 'axis',
            axisPointer: { type: 'cross', label: { backgroundColor: '#1e2327' } },
        },
        legend: {
            data: ['create', 'delete', 'rename', 'modify'],
            textStyle: { color: '#98a2b3' },
            top: 0,
        },
        grid: { left: 50, right: 20, top: 40, bottom: 40 },
        xAxis: {
            type: 'category',
            boundaryGap: false,
            data: data.operation_trend.map(t => dayjs(t.date).format('MM-DD')),
            axisLine: { lineStyle: { color: '#3c4752' } },
            axisLabel: { color: '#98a2b3' },
        },
        yAxis: {
            type: 'value',
            axisLine: { lineStyle: { color: '#3c4752' } },
            axisLabel: { color: '#98a2b3' },
            splitLine: { lineStyle: { color: '#2a333d' } },
        },
        series: ['create', 'delete', 'rename', 'modify'].map((op, idx) => ({
            name: op,
            type: 'line',
            smooth: true,
            stack: null,
            data: data.operation_trend.map(t => t.by_operation[op] || 0),
            lineStyle: { width: 2 },
            itemStyle: {
                color: ['#00bfb3', '#fec514', '#bd2719', '#1ea7fd'][idx],
            },
            areaStyle: {
                opacity: 0.15,
            },
        })),
    };

    const extOption = {
        tooltip: { trigger: 'item', formatter: '{b}: {c} ({d}%)' },
        series: [{
            type: 'pie',
            radius: ['45%', '70%'],
            center: ['50%', '50%'],
            avoidLabelOverlap: true,
            itemStyle: {
                borderRadius: 6,
                borderColor: '#1e2327',
                borderWidth: 2,
            },
            label: { color: '#98a2b3', fontSize: 12 },
            labelLine: { lineStyle: { color: '#3c4752' } },
            data: data.extension_stats.map(e => ({
                name: e.extension || '(无)',
                value: e.count,
            })),
        }],
        color: ['#00bfb3', '#fec514', '#bd2719', '#1ea7fd', '#343741', '#98a2b3', '#00a699'],
    };

    const totalOps = data.operation_trend.reduce((sum, t) => sum + t.total, 0);
    const uniqueUsers = data.top_users.length;

    return (
        <div className="dashboard">
            <div className="dashboard-toolbar">
                <div className="time-range-selector">
                    <span className="label">时间范围:</span>
                    <div className="range-buttons">
                        {[1, 7, 30, 90].map(d => (
                            <button
                                key={d}
                                className={`range-btn ${days === d ? 'active' : ''}`}
                                onClick={() => setDays(d)}
                            >
                                {d === 1 ? '24小时' : `${d}天`}
                            </button>
                        ))}
                    </div>
                    <button className="refresh-btn" onClick={fetchData} disabled={loading}>
                        🔄 {loading ? '刷新中...' : '刷新'}
                    </button>
                </div>
            </div>

            <div className="stats-cards">
                <div className="stat-card">
                    <div className="stat-icon" style={{ background: 'linear-gradient(135deg, #00bfb3, #00a699)' }}>
                        📁
                    </div>
                    <div className="stat-content">
                        <div className="stat-value">{totalOps.toLocaleString()}</div>
                        <div className="stat-label">总操作数</div>
                    </div>
                </div>
                <div className="stat-card">
                    <div className="stat-icon" style={{ background: 'linear-gradient(135deg, #1ea7fd, #178ad6)' }}>
                        👥
                    </div>
                    <div className="stat-content">
                        <div className="stat-value">{uniqueUsers}</div>
                        <div className="stat-label">活跃用户</div>
                    </div>
                </div>
                <div className="stat-card">
                    <div className="stat-icon" style={{ background: 'linear-gradient(135deg, #fec514, #e6b012)' }}>
                        🗂️
                    </div>
                    <div className="stat-content">
                        <div className="stat-value">{data.extension_stats.length}</div>
                        <div className="stat-label">文件类型</div>
                    </div>
                </div>
                <div className="stat-card">
                    <div className="stat-icon" style={{ background: health.elasticsearch ? 'linear-gradient(135deg, #00bfb3, #00a699)' : 'linear-gradient(135deg, #bd2719, #a82216)' }}>
                        {health.elasticsearch && health.smb_connected ? '✓' : '!'}
                    </div>
                    <div className="stat-content">
                        <div className="stat-value" style={{ fontSize: '14px' }}>
                            {health.elasticsearch ? 'ES正常' : 'ES离线'}
                        </div>
                        <div className="stat-label">
                            {health.smb_connected ? 'SMB已连接' : 'SMB未连接'}
                        </div>
                    </div>
                </div>
            </div>

            <div className="charts-grid">
                <div className="chart-card wide">
                    <div className="chart-header">
                        <h3>操作趋势</h3>
                        <span className="chart-subtitle">最近{days}天每日操作分布</span>
                    </div>
                    <div className="chart-body">
                        <ReactECharts option={trendOption} style={{ height: 280 }} />
                    </div>
                </div>

                <div className="chart-card">
                    <div className="chart-header">
                        <h3>TOP活跃用户</h3>
                        <span className="chart-subtitle">最近{days}天</span>
                    </div>
                    <div className="chart-body">
                        {data.top_users.length > 0 ? (
                            <ReactECharts option={topUsersOption} style={{ height: 280 }} />
                        ) : (
                            <div className="empty-state">暂无数据</div>
                        )}
                    </div>
                </div>

                <div className="chart-card">
                    <div className="chart-header">
                        <h3>文件类型分布</h3>
                        <span className="chart-subtitle">按扩展名统计</span>
                    </div>
                    <div className="chart-body">
                        {data.extension_stats.length > 0 ? (
                            <ReactECharts option={extOption} style={{ height: 280 }} />
                        ) : (
                            <div className="empty-state">暂无数据</div>
                        )}
                    </div>
                </div>
            </div>
        </div>
    );
};

export default Dashboard;
