import React, { useEffect, useState } from 'react';
import './StrategyManager.css';

const API_BASE = (import.meta.env.VITE_API_BASE || '');

const fetchJSON = async (url, options) => {
  const res = await fetch(url, options);
  if (!res.ok) {
    const err = await res.json().catch(() => ({}));
    throw new Error(err.error || `HTTP ${res.status}`);
  }
  return res.json();
};

const formatTime = (ts) => {
  if (!ts) return '—';
  return new Date(ts).toLocaleString();
};

const ACTION_HINTS = {
  login: '用户登录',
  order: '创建订单',
  post:  '发布内容',
};

const Toast = ({ type, message, onClose }) => {
  useEffect(() => {
    const id = setTimeout(onClose, 3000);
    return () => clearTimeout(id);
  }, [onClose]);
  return <div className={`toast ${type}`}>{message}</div>;
};

const StrategyConfig = ({ strategies, onSave, submitting, operator, reason, setOperator, setReason }) => {
  const [local, setLocal] = useState({});

  useEffect(() => {
    const init = {};
    for (const s of strategies) {
      init[s.action_type] = { ...s };
    }
    setLocal(init);
  }, [strategies]);

  const dirty = (actionType) => {
    const orig = strategies.find((s) => s.action_type === actionType);
    const cur = local[actionType];
    if (!orig || !cur) return false;
    return cur.max_burst !== orig.max_burst
      || cur.count_per_period !== orig.count_per_period
      || cur.period_seconds !== orig.period_seconds;
  };

  const save = async (actionType) => {
    if (!dirty(actionType)) return;
    const cur = local[actionType];
    await onSave({
      action_type: actionType,
      max_burst: cur.max_burst,
      count_per_period: cur.count_per_period,
      period_seconds: cur.period_seconds,
      operator,
      reason,
    });
  };

  const reset = (actionType) => {
    const orig = strategies.find((s) => s.action_type === actionType);
    setLocal((prev) => ({ ...prev, [actionType]: { ...orig } }));
  };

  return (
    <div>
      <div className="audit-form">
        <div className="field">
          <label>操作人</label>
          <input
            type="text"
            value={operator}
            onChange={(e) => setOperator(e.target.value)}
            placeholder="e.g. zhangsan"
          />
        </div>
        <div className="field">
          <label>变更原因</label>
          <input
            type="text"
            value={reason}
            onChange={(e) => setReason(e.target.value)}
            placeholder="e.g. 大促期间调整登录阈值"
          />
        </div>
        <div />
      </div>

      <div className="strategy-config-grid">
        {Object.entries(local).map(([actionType, cfg]) => (
          <div key={actionType} className="strategy-card">
            <div className="action-label">{actionType}</div>
            <div className="action-hint">{ACTION_HINTS[actionType] || '—'}</div>

            <div className="field">
              <label>突发容量 (max_burst)</label>
              <input
                type="number"
                min="1"
                value={cfg.max_burst}
                onChange={(e) => setLocal((p) => ({
                  ...p,
                  [actionType]: { ...p[actionType], max_burst: Number(e.target.value) },
                }))}
              />
            </div>

            <div className="field">
              <label>周期计数 (count_per_period)</label>
              <input
                type="number"
                min="1"
                value={cfg.count_per_period}
                onChange={(e) => setLocal((p) => ({
                  ...p,
                  [actionType]: { ...p[actionType], count_per_period: Number(e.target.value) },
                }))}
              />
            </div>

            <div className="field">
              <label>周期秒数 (period_seconds)</label>
              <input
                type="number"
                min="1"
                value={cfg.period_seconds}
                onChange={(e) => setLocal((p) => ({
                  ...p,
                  [actionType]: { ...p[actionType], period_seconds: Number(e.target.value) },
                }))}
              />
            </div>

            <div className="actions">
              <button
                className="btn btn-secondary"
                onClick={() => reset(actionType)}
                disabled={!dirty(actionType) || submitting}
              >
                重置
              </button>
              <button
                className="btn btn-primary"
                onClick={() => save(actionType)}
                disabled={!dirty(actionType) || submitting || !operator}
                title={!operator ? '请先填写操作人' : ''}
              >
                保存
              </button>
            </div>
          </div>
        ))}
      </div>
    </div>
  );
};

const History = ({ entries }) => {
  if (entries.length === 0) {
    return <div className="empty-state">暂无历史记录</div>;
  }
  return (
    <table className="history-table">
      <thead>
        <tr>
          <th>时间</th>
          <th>操作人</th>
          <th>动作</th>
          <th>变更前</th>
          <th>变更后</th>
          <th>原因</th>
        </tr>
      </thead>
      <tbody>
        {entries.map((e) => (
          <tr key={e.id}>
            <td>{formatTime(e.created_at)}</td>
            <td>{e.operator}</td>
            <td>
              <code className="code-block">{e.action_type}</code>
            </td>
            <td className="code-block">
              {e.before ? `burst=${e.before.max_burst}, count=${e.before.count_per_period}, period=${e.before.period_seconds}s` : '—'}
            </td>
            <td className="code-block">
              {e.after ? `burst=${e.after.max_burst}, count=${e.after.count_per_period}, period=${e.after.period_seconds}s` : '—'}
            </td>
            <td>{e.reason || '—'}</td>
          </tr>
        ))}
      </tbody>
    </table>
  );
};

const Audit = ({ entries }) => {
  if (entries.length === 0) {
    return <div className="empty-state">暂无审计记录</div>;
  }
  return (
    <table className="history-table">
      <thead>
        <tr>
          <th>时间</th>
          <th>操作人</th>
          <th>操作</th>
          <th>目标</th>
          <th>详情</th>
          <th>原因</th>
        </tr>
      </thead>
      <tbody>
        {entries.map((a) => (
          <tr key={a.id}>
            <td>{formatTime(a.created_at)}</td>
            <td>{a.operator}</td>
            <td>
              <span className={`badge ${a.action === 'UPDATE' ? 'badge-update' : 'badge-rollback'}`}>
                {a.action}
              </span>
            </td>
            <td>
              <code className="code-block">{a.target}</code>
            </td>
            <td>
              {a.action === 'ROLLBACK'
                ? `→ version ${a.target_version || '?'}`
                : <span className="code-block">{a.before_json} → {a.after_json}</span>}
            </td>
            <td>{a.reason || '—'}</td>
          </tr>
        ))}
      </tbody>
    </table>
  );
};

const StrategyManager = () => {
  const [tab, setTab] = useState('config');
  const [strategies, setStrategies] = useState([]);
  const [history, setHistory] = useState([]);
  const [audit, setAudit] = useState([]);
  const [submitting, setSubmitting] = useState(false);
  const [toast, setToast] = useState(null);
  const [operator, setOperator] = useState(localStorage.getItem('rc_operator') || '');
  const [reason, setReason] = useState('');

  useEffect(() => {
    localStorage.setItem('rc_operator', operator);
  }, [operator]);

  const load = async () => {
    try {
      if (tab === 'config') {
        const d = await fetchJSON(`${API_BASE}/api/strategies`);
        setStrategies(d.rate_limits || []);
      } else if (tab === 'history') {
        const d = await fetchJSON(`${API_BASE}/api/strategies/history?limit=20`);
        setHistory(d.entries || []);
      } else if (tab === 'audit') {
        const d = await fetchJSON(`${API_BASE}/api/strategies/audit?limit=100`);
        setAudit(d.entries || []);
      }
    } catch (err) {
      setToast({ type: 'error', message: `加载失败: ${err.message}` });
    }
  };

  useEffect(() => { load(); }, [tab]);

  const save = async (payload) => {
    setSubmitting(true);
    try {
      await fetchJSON(`${API_BASE}/api/strategies`, {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify(payload),
      });
      setToast({ type: 'success', message: `策略已更新: ${payload.action_type}` });
      setReason('');
      load();
    } catch (err) {
      setToast({ type: 'error', message: `更新失败: ${err.message}` });
    } finally {
      setSubmitting(false);
    }
  };

  const rollback = async () => {
    if (!operator) {
      setToast({ type: 'error', message: '请先在"策略配置"中填写操作人' });
      return;
    }
    if (!confirm('确认回滚到上一个策略版本？此操作也会记录审计日志。')) return;
    setSubmitting(true);
    try {
      const d = await fetchJSON(`${API_BASE}/api/strategies/rollback`, {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify({ operator, reason: 'rollback' }),
      });
      setToast({ type: 'success', message: `已回滚到版本 ${d.rolled_back_to_version}` });
      load();
    } catch (err) {
      setToast({ type: 'error', message: `回滚失败: ${err.message}` });
    } finally {
      setSubmitting(false);
    }
  };

  return (
    <div>
      <div className="header-bar">
        <h2>策略管理</h2>
        {tab === 'history' && (
          <button className="btn btn-danger" onClick={rollback} disabled={submitting}>
            ⟲ 一键回滚到上一版本
          </button>
        )}
      </div>

      <div className="strategy-tabs">
        <button className={tab === 'config' ? 'active' : ''} onClick={() => setTab('config')}>
          策略配置
        </button>
        <button className={tab === 'history' ? 'active' : ''} onClick={() => setTab('history')}>
          历史版本
        </button>
        <button className={tab === 'audit' ? 'active' : ''} onClick={() => setTab('audit')}>
          审计日志
        </button>
      </div>

      {tab === 'config' && (
        <StrategyConfig
          strategies={strategies}
          onSave={save}
          submitting={submitting}
          operator={operator}
          reason={reason}
          setOperator={setOperator}
          setReason={setReason}
        />
      )}
      {tab === 'history' && <History entries={history} />}
      {tab === 'audit' && <Audit entries={audit} />}

      {toast && (
        <Toast
          type={toast.type}
          message={toast.message}
          onClose={() => setToast(null)}
        />
      )}
    </div>
  );
};

export default StrategyManager;
