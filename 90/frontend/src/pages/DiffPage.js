import React, { useState, useEffect } from 'react';
import { diffApi, commitApi, branchApi } from '../services/api';

function DiffPage() {
  const [branch, setBranch] = useState('main');
  const [branches, setBranches] = useState([]);
  const [commits, setCommits] = useState([]);
  const [commitA, setCommitA] = useState('');
  const [commitB, setCommitB] = useState('');
  const [diffResult, setDiffResult] = useState(null);
  const [loading, setLoading] = useState(false);
  const [error, setError] = useState(null);

  useEffect(() => {
    fetchBranches();
  }, []);

  useEffect(() => {
    fetchCommits();
  }, [branch]);

  const fetchBranches = async () => {
    try {
      const res = await branchApi.list();
      setBranches(res.data);
    } catch (err) {
      console.error('Failed to fetch branches:', err);
    }
  };

  const fetchCommits = async () => {
    try {
      const res = await commitApi.list(branch);
      setCommits(res.data);
    } catch (err) {
      console.error('Failed to fetch commits:', err);
    }
  };

  const handleDiff = async () => {
    if (!commitA || !commitB) {
      setError('请选择两个提交进行对比');
      return;
    }

    setLoading(true);
    setError(null);
    setDiffResult(null);

    try {
      const res = await diffApi.betweenCommits(commitA, commitB);
      setDiffResult(res.data);
    } catch (err) {
      setError(err.response?.data?.error || '差异对比失败');
    } finally {
      setLoading(false);
    }
  };

  const renderDiffLines = (lines, type) => {
    if (!lines || lines.length === 0) return null;

    return lines.map((line, idx) => (
      <div key={`${type}-${idx}`} className={`diff-line diff-${type}`}>
        <span className="diff-line-number">{line.line_number}</span>
        {type === 'added' ? '+' : type === 'removed' ? '-' : ' '}
        {line.content}
      </div>
    ));
  };

  return (
    <div className="page-container">
      <h2 className="page-title">差异对比</h2>

      <div className="form-group">
        <label className="form-label">选择分支</label>
        <select
          className="form-select"
          value={branch}
          onChange={(e) => setBranch(e.target.value)}
        >
          {branches.map((b) => (
            <option key={b.name} value={b.name}>
              {b.name}
            </option>
          ))}
        </select>
      </div>

      <div style={{ display: 'grid', gridTemplateColumns: '1fr 1fr', gap: '16px' }}>
        <div className="form-group">
          <label className="form-label">提交 A（旧版本）</label>
          <select
            className="form-select"
            value={commitA}
            onChange={(e) => setCommitA(e.target.value)}
          >
            <option value="">请选择提交</option>
            {commits.map((c) => (
              <option key={c.id} value={c.id}>
                {c.message} ({c.id.substring(0, 8)}...)
              </option>
            ))}
          </select>
        </div>

        <div className="form-group">
          <label className="form-label">提交 B（新版本）</label>
          <select
            className="form-select"
            value={commitB}
            onChange={(e) => setCommitB(e.target.value)}
          >
            <option value="">请选择提交</option>
            {commits.map((c) => (
              <option key={c.id} value={c.id}>
                {c.message} ({c.id.substring(0, 8)}...)
              </option>
            ))}
          </select>
        </div>
      </div>

      {error && (
        <div className="alert alert-error">{error}</div>
      )}

      <button
        className="btn btn-primary"
        onClick={handleDiff}
        disabled={loading || !commitA || !commitB}
        style={{ marginTop: '16px' }}
      >
        {loading ? '对比中...' : '对比差异'}
      </button>

      {diffResult && (
        <div style={{ marginTop: '24px' }}>
          <h3 style={{ marginBottom: '16px' }}>差异结果</h3>

          <div className="diff-container">
            <div className="diff-header">
              <span className="badge badge-danger" style={{ marginRight: '8px' }}>
                -{diffResult.removed?.length || 0} 行删除
              </span>
              <span className="badge badge-success">
                +{diffResult.added?.length || 0} 行添加
              </span>
            </div>
            <div className="diff-content">
              {diffResult.removed?.length === 0 && diffResult.added?.length === 0 ? (
                <div className="diff-line diff-context" style={{ textAlign: 'center', padding: '20px' }}>
                  两个版本完全相同
                </div>
              ) : (
                <>
                  {renderDiffLines(diffResult.removed, 'removed')}
                  {renderDiffLines(diffResult.added, 'added')}
                </>
              )}
            </div>
          </div>
        </div>
      )}
    </div>
  );
}

export default DiffPage;
