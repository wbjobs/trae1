import React, { useState, useEffect } from 'react';
import { useNavigate } from 'react-router-dom';
import { branchApi, mergeApi } from '../services/api';
import MergeStrategyConfig from '../components/MergeStrategyConfig';

function BranchPage() {
  const navigate = useNavigate();
  const [branches, setBranches] = useState([]);
  const [loading, setLoading] = useState(false);
  const [showCreateModal, setShowCreateModal] = useState(false);
  const [showMergeModal, setShowMergeModal] = useState(false);
  const [showStrategyConfig, setShowStrategyConfig] = useState(false);
  const [newBranch, setNewBranch] = useState({ name: '', from: 'main' });
  const [mergeData, setMergeData] = useState({ source: '', target: 'main', author: '', message: '' });
  const [mergeStrategy, setMergeStrategy] = useState(null);
  const [error, setError] = useState(null);
  const [success, setSuccess] = useState(null);

  useEffect(() => {
    fetchBranches();
  }, []);

  const fetchBranches = async () => {
    setLoading(true);
    try {
      const res = await branchApi.list();
      setBranches(res.data);
    } catch (err) {
      console.error('Failed to fetch branches:', err);
    } finally {
      setLoading(false);
    }
  };

  const handleCreateBranch = async () => {
    if (!newBranch.name) {
      setError('请输入分支名称');
      return;
    }

    try {
      await branchApi.create(newBranch);
      setSuccess(`分支 "${newBranch.name}" 创建成功`);
      setNewBranch({ name: '', from: 'main' });
      setShowCreateModal(false);
      setError(null);
      fetchBranches();
    } catch (err) {
      setError(err.response?.data?.error || '创建分支失败');
    }
  };

  const handleDeleteBranch = async (name) => {
    if (name === 'main') {
      setError('不能删除 main 分支');
      return;
    }

    if (!window.confirm(`确定要删除分支 "${name}" 吗？`)) {
      return;
    }

    try {
      await branchApi.delete(name);
      setSuccess(`分支 "${name}" 已删除`);
      fetchBranches();
    } catch (err) {
      setError(err.response?.data?.error || '删除分支失败');
    }
  };

  const handleMerge = async () => {
    if (!mergeData.source || !mergeData.target) {
      setError('请选择源分支和目标分支');
      return;
    }

    setLoading(true);
    setError(null);

    try {
      const requestData = {
        ...mergeData,
        strategy: mergeStrategy,
      };

      const res = await mergeApi.merge(requestData);

      if (res.data.success) {
        setSuccess(res.data.message);
        setShowMergeModal(false);
        setMergeData({ source: '', target: 'main', author: '', message: '' });
        fetchBranches();
      } else if (res.data.conflicts) {
        sessionStorage.setItem('pendingMerge', JSON.stringify(res.data));
        sessionStorage.setItem('mergeTargetBranch', mergeData.target);
        navigate('/merge/resolve');
      } else {
        setError(res.data.message);
      }
    } catch (err) {
      setError(err.response?.data?.error || '合并失败');
    } finally {
      setLoading(false);
    }
  };

  return (
    <div className="page-container">
      <div style={{ display: 'flex', justifyContent: 'space-between', alignItems: 'center', marginBottom: '24px' }}>
        <h2 className="page-title" style={{ marginBottom: 0, paddingBottom: 0, borderBottom: 'none' }}>
          分支管理
        </h2>
        <div style={{ display: 'flex', gap: '8px' }}>
          <button
            className="btn btn-secondary"
            onClick={() => setShowMergeModal(true)}
          >
            🔀 合并分支
          </button>
          <button
            className="btn btn-primary"
            onClick={() => setShowCreateModal(true)}
          >
            + 创建分支
          </button>
        </div>
      </div>

      {error && (
        <div className="alert alert-error">{error}</div>
      )}

      {success && (
        <div className="alert alert-success">{success}</div>
      )}

      {loading ? (
        <div className="loading">
          <div className="spinner"></div>
          加载中...
        </div>
      ) : (
        <div className="branch-list">
          {branches.length === 0 ? (
            <div className="alert alert-info">暂无分支</div>
          ) : (
            branches.map((branch) => (
              <div key={branch.name} className="branch-card">
                <div style={{ display: 'flex', justifyContent: 'space-between', alignItems: 'center' }}>
                  <h3 style={{ fontSize: '18px', fontWeight: 600 }}>
                    {branch.name === 'main' && '🌿 '}
                    {branch.name}
                  </h3>
                  {branch.name === 'main' && (
                    <span className="badge badge-primary">默认</span>
                  )}
                </div>
                <div className="card-content" style={{ marginTop: '8px' }}>
                  <p><strong>最新提交:</strong></p>
                  <code style={{ fontSize: '12px', color: '#6b7280' }}>
                    {branch.commit?.substring(0, 20)}...
                  </code>
                </div>
                {branch.name !== 'main' && (
                  <div className="branch-actions">
                    <button
                      className="btn btn-danger btn-sm"
                      onClick={() => handleDeleteBranch(branch.name)}
                    >
                      删除
                    </button>
                  </div>
                )}
              </div>
            ))
          )}
        </div>
      )}

      {showCreateModal && (
        <div className="modal-overlay" onClick={() => setShowCreateModal(false)}>
          <div className="modal" onClick={(e) => e.stopPropagation()}>
            <h3 className="modal-title">创建分支</h3>

            <div className="form-group">
              <label className="form-label">分支名称</label>
              <input
                type="text"
                className="form-input"
                value={newBranch.name}
                onChange={(e) => setNewBranch({ ...newBranch, name: e.target.value })}
                placeholder="例如: feature/new-config"
              />
            </div>

            <div className="form-group">
              <label className="form-label">基于分支</label>
              <select
                className="form-select"
                value={newBranch.from}
                onChange={(e) => setNewBranch({ ...newBranch, from: e.target.value })}
              >
                {branches.map((b) => (
                  <option key={b.name} value={b.name}>
                    {b.name}
                  </option>
                ))}
              </select>
            </div>

            {error && (
              <div className="alert alert-error">{error}</div>
            )}

            <div className="modal-actions">
              <button
                className="btn btn-secondary"
                onClick={() => { setShowCreateModal(false); setError(null); }}
              >
                取消
              </button>
              <button
                className="btn btn-primary"
                onClick={handleCreateBranch}
              >
                创建
              </button>
            </div>
          </div>
        </div>
      )}

      {showMergeModal && (
        <div className="modal-overlay" onClick={() => setShowMergeModal(false)}>
          <div className="modal" onClick={(e) => e.stopPropagation()} style={{ maxWidth: '700px' }}>
            <h3 className="modal-title">🔀 合并分支</h3>

            <div style={{ display: 'grid', gridTemplateColumns: '1fr 1fr', gap: '16px' }}>
              <div className="form-group">
                <label className="form-label">源分支（要合并的分支）</label>
                <select
                  className="form-select"
                  value={mergeData.source}
                  onChange={(e) => setMergeData({ ...mergeData, source: e.target.value })}
                >
                  <option value="">请选择源分支</option>
                  {branches
                    .filter((b) => b.name !== mergeData.target)
                    .map((b) => (
                      <option key={b.name} value={b.name}>
                        {b.name}
                      </option>
                    ))}
                </select>
              </div>

              <div className="form-group">
                <label className="form-label">目标分支（合并到的分支）</label>
                <select
                  className="form-select"
                  value={mergeData.target}
                  onChange={(e) => setMergeData({ ...mergeData, target: e.target.value })}
                >
                  {branches
                    .filter((b) => b.name !== mergeData.source)
                    .map((b) => (
                      <option key={b.name} value={b.name}>
                        {b.name}
                      </option>
                    ))}
                </select>
              </div>
            </div>

            <div className="form-group">
              <label className="form-label">提交消息</label>
              <input
                type="text"
                className="form-input"
                value={mergeData.message}
                onChange={(e) => setMergeData({ ...mergeData, message: e.target.value })}
                placeholder={`Merge branch '${mergeData.source}' into '${mergeData.target}'`}
              />
            </div>

            <div className="form-group">
              <label className="form-label">作者</label>
              <input
                type="text"
                className="form-input"
                value={mergeData.author}
                onChange={(e) => setMergeData({ ...mergeData, author: e.target.value })}
                placeholder="请输入作者名称"
              />
            </div>

            <div className="form-group">
              <button
                type="button"
                className="btn btn-secondary btn-sm"
                onClick={() => setShowStrategyConfig(!showStrategyConfig)}
                style={{ marginBottom: '8px' }}
              >
                ⚙️ {showStrategyConfig ? '隐藏' : '显示'}合并策略配置
              </button>

              {showStrategyConfig && (
                <MergeStrategyConfig
                  strategy={mergeStrategy}
                  onChange={setMergeStrategy}
                />
              )}
            </div>

            {error && (
              <div className="alert alert-error">{error}</div>
            )}

            <div className="alert alert-info" style={{ fontSize: '12px' }}>
              <p><strong>💡 提示：</strong></p>
              <ul style={{ marginLeft: '20px', marginTop: '4px' }}>
                <li>使用 AST 语义合并，支持 YAML/JSON 结构化合并</li>
                <li>大文件（如 10MB+ Kubernetes manifest）也能正确处理</li>
                <li>冲突时会自动进入可视化冲突解决界面</li>
              </ul>
            </div>

            <div className="modal-actions">
              <button
                className="btn btn-secondary"
                onClick={() => { setShowMergeModal(false); setError(null); setShowStrategyConfig(false); }}
              >
                取消
              </button>
              <button
                className="btn btn-primary"
                onClick={handleMerge}
                disabled={loading || !mergeData.source || !mergeData.target}
              >
                {loading ? (
                  <>
                    <span className="spinner" style={{ display: 'inline-block', width: '14px', height: '14px', marginRight: '8px', verticalAlign: 'middle' }}></span>
                    合并中...
                  </>
                ) : '开始合并'}
              </button>
            </div>
          </div>
        </div>
      )}
    </div>
  );
}

export default BranchPage;
