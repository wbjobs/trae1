import React, { useState, useEffect, useCallback } from 'react';
import { auditApi } from '../api/client';
import dayjs from 'dayjs';

const OperationTypeBadge = ({ type }) => {
    const styles = {
        create: { bg: '#00bfb3', text: '创建' },
        delete: { bg: '#bd2719', text: '删除' },
        rename: { bg: '#fec514', text: '重命名' },
        modify: { bg: '#1ea7fd', text: '修改' },
        access: { bg: '#98a2b3', text: '访问' },
    };
    const s = styles[type] || styles.access;
    return (
        <span className="op-badge" style={{ background: s.bg }}>
            {s.text}
        </span>
    );
};

const EventLog = () => {
    const [filters, setFilters] = useState({
        username: '',
        extensions: '',
        operation_type: '',
        file_path: '',
        source_ip: '',
    });
    const [timeRange, setTimeRange] = useState('7d');
    const [events, setEvents] = useState([]);
    const [total, setTotal] = useState(0);
    const [page, setPage] = useState(1);
    const [pageSize] = useState(50);
    const [loading, setLoading] = useState(false);
    const [scrollId, setScrollId] = useState(null);

    const getTimeRange = () => {
        const now = dayjs();
        let start;
        switch (timeRange) {
            case '1h': start = now.subtract(1, 'hour'); break;
            case '24h': start = now.subtract(24, 'hour'); break;
            case '7d': start = now.subtract(7, 'day'); break;
            case '30d': start = now.subtract(30, 'day'); break;
            case '90d': start = now.subtract(90, 'day'); break;
            default: start = now.subtract(7, 'day');
        }
        return {
            start_time: start.unix(),
            end_time: now.unix(),
        };
    };

    const fetchEvents = useCallback(async (reset = false) => {
        setLoading(true);
        try {
            const range = getTimeRange();
            const params = {
                ...range,
                size: pageSize,
                username: filters.username || undefined,
                extensions: filters.extensions || undefined,
                operation_type: filters.operation_type || undefined,
                file_path: filters.file_path || undefined,
                source_ip: filters.source_ip || undefined,
                scroll_id: reset ? null : scrollId,
            };

            const res = await auditApi.queryEvents(params);
            const newEvents = res.data.events || [];
            setEvents(reset ? newEvents : [...events, ...newEvents]);
            setTotal(res.data.total || 0);
            setScrollId(res.data.scroll_id || null);
        } catch (error) {
            console.error('Failed to fetch events:', error);
        } finally {
            setLoading(false);
        }
    }, [filters, timeRange, pageSize, scrollId]);

    const handleSearch = () => {
        setScrollId(null);
        setPage(1);
        fetchEvents(true);
    };

    const handleLoadMore = () => {
        setPage(p => p + 1);
        fetchEvents(false);
    };

    useEffect(() => {
        handleSearch();
    }, [timeRange]);

    const allExtensions = [
        { value: '.doc,.docx', label: 'Word (.doc/.docx)' },
        { value: '.xls,.xlsx', label: 'Excel (.xls/.xlsx)' },
        { value: '.pdf', label: 'PDF (.pdf)' },
        { value: '.ppt,.pptx', label: 'PowerPoint (.ppt/.pptx)' },
        { value: '.txt', label: 'Text (.txt)' },
        { value: '.zip,.rar', label: '压缩包 (.zip/.rar)' },
    ];

    return (
        <div className="event-log">
            <div className="log-toolbar">
                <div className="filter-group">
                    <div className="filter-item">
                        <label>时间范围</label>
                        <select value={timeRange} onChange={(e) => setTimeRange(e.target.value)}>
                            <option value="1h">最近1小时</option>
                            <option value="24h">最近24小时</option>
                            <option value="7d">最近7天</option>
                            <option value="30d">最近30天</option>
                            <option value="90d">最近90天</option>
                        </select>
                    </div>
                    <div className="filter-item">
                        <label>操作类型</label>
                        <select
                            value={filters.operation_type}
                            onChange={(e) => setFilters({ ...filters, operation_type: e.target.value })}
                        >
                            <option value="">全部</option>
                            <option value="create">创建</option>
                            <option value="delete">删除</option>
                            <option value="rename">重命名</option>
                            <option value="modify">修改</option>
                        </select>
                    </div>
                    <div className="filter-item">
                        <label>文件类型</label>
                        <select
                            value={filters.extensions}
                            onChange={(e) => setFilters({ ...filters, extensions: e.target.value })}
                        >
                            <option value="">全部</option>
                            {allExtensions.map(ext => (
                                <option key={ext.value} value={ext.value}>{ext.label}</option>
                            ))}
                        </select>
                    </div>
                    <div className="filter-item">
                        <label>用户名</label>
                        <input
                            type="text"
                            placeholder="输入用户名"
                            value={filters.username}
                            onChange={(e) => setFilters({ ...filters, username: e.target.value })}
                        />
                    </div>
                    <div className="filter-item">
                        <label>源IP</label>
                        <input
                            type="text"
                            placeholder="输入IP地址"
                            value={filters.source_ip}
                            onChange={(e) => setFilters({ ...filters, source_ip: e.target.value })}
                        />
                    </div>
                    <div className="filter-item">
                        <label>文件路径</label>
                        <input
                            type="text"
                            placeholder="关键字搜索"
                            value={filters.file_path}
                            onChange={(e) => setFilters({ ...filters, file_path: e.target.value })}
                        />
                    </div>
                </div>
                <button className="search-btn" onClick={handleSearch} disabled={loading}>
                    🔍 查询
                </button>
            </div>

            <div className="log-summary">
                共找到 <strong>{total}</strong> 条记录，显示 {events.length} 条
            </div>

            <div className="log-table-container">
                <table className="log-table">
                    <thead>
                        <tr>
                            <th style={{ width: 100 }}>时间</th>
                            <th style={{ width: 100 }}>操作</th>
                            <th style={{ width: 120 }}>用户</th>
                            <th style={{ width: 120 }}>源IP</th>
                            <th>文件路径</th>
                            <th style={{ width: 80 }}>类型</th>
                            <th style={{ width: 100 }}>大小</th>
                        </tr>
                    </thead>
                    <tbody>
                        {events.map((evt, idx) => (
                            <tr key={idx} className="log-row">
                                <td className="time-cell">
                                    {dayjs(evt.iso_timestamp).format('YYYY-MM-DD HH:mm:ss')}
                                </td>
                                <td>
                                    <OperationTypeBadge type={evt.operation_type} />
                                </td>
                                <td className="user-cell">
                                    {evt.username || '-'}
                                </td>
                                <td className="ip-cell">
                                    {evt.source_ip || '-'}
                                </td>
                                <td className="path-cell">
                                    {evt.operation_type === 'rename' && evt.old_file_path && (
                                        <>
                                            <span className="old-path">{evt.old_file_path}</span>
                                            <span className="arrow">→</span>
                                        </>
                                    )}
                                    <span className="file-path">{evt.file_path}</span>
                                </td>
                                <td className="ext-cell">
                                    <span className="ext-badge">{evt.file_extension || '(无)'}</span>
                                </td>
                                <td className="size-cell">
                                    {evt.file_size > 1048576
                                        ? `${(evt.file_size / 1048576).toFixed(1)} MB`
                                        : evt.file_size > 1024
                                        ? `${(evt.file_size / 1024).toFixed(1)} KB`
                                        : `${evt.file_size} B`}
                                </td>
                            </tr>
                        ))}
                        {events.length === 0 && !loading && (
                            <tr>
                                <td colSpan={7} className="empty-state">
                                    暂无操作记录
                                </td>
                            </tr>
                        )}
                    </tbody>
                </table>
            </div>

            {events.length < total && (
                <div className="load-more-container">
                    <button
                        className="load-more-btn"
                        onClick={handleLoadMore}
                        disabled={loading}
                    >
                        {loading ? '加载中...' : `加载更多 (剩余 ${total - events.length} 条)`}
                    </button>
                </div>
            )}
        </div>
    );
};

export default EventLog;
