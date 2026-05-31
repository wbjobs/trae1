import React, { useState, useEffect } from 'react';
import { tagApi, commitApi, branchApi } from '../services/api';

function TagPage() {
  const [tags, setTags] = useState([]);
  const [commits, setCommits] = useState([]);
  const [branches, setBranches] = useState([]);
  const [loading, setLoading] = useState(false);
  const [showCreateModal, setShowCreateModal] = useState(false);
  const [newTag, setNewTag] = useState({ name: '', commit_id: '', message: '' });
  const [selectedBranch, setSelectedBranch] = useState('main');
  const [error, setError] = useState(null);
  const [success, setSuccess] = useState(null);

  useEffect(() => {
    fetchTags();
    fetchBranches();
  }, []);

  useEffect(() => {
    fetchCommits();
  }, [selectedBranch]);

  const fetchTags = async () => {
    setLoading(true);
    try {
      const res = await tagApi.list();
      setTags(res.data);
    } catch (err) {
      console.error('Failed to fetch tags:', err);
    } finally {
      setLoading(false);
    }
  };

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
      const res = await commitApi.list(selectedBranch);
      setCommits(res.data);
    } catch (err) {
      console.error('Failed to fetch commits:', err);
    }
  };

  const handleCreateTag = async () => {
    if (!newTag.name || !newTag.commit_id) {
      setError('请填写标签名称和选择提交');
      return;
    }

    try {
      await tagApi.create(newTag);
      setSuccess(`标签 "${newTag.name}" 创建成功`);
      setNewTag({ name: '', commit_id: '', message: '' });
      setShowCreateModal(false);
      setError(null);
      fetchTags();
    } catch (err) {
      setError(err.response?.data?.error || '创建标签失败');
    }
  };

  const handleDeleteTag = async (name) => {
    if (!window.confirm(`确定要删除标签 "${name}" 吗？`)) {
      return;
    }

    try {
      await tagApi.delete(name);
      setSuccess(`标签 "${name}" 已删除`);
      fetchTags();
    } catch (err) {
      setError(err.response?.data?.error || '删除标签失败');
    }
  };

  const handlePullByTag = async (tagName) => {
    try {
      const response = await fetch(
        `http://localhost:8080/api/configs/tag/${tagName}`
      );
      const content = await response.text();

      const blob = new Blob([content], { type: 'text/plain' });
      const url = URL.createObjectURL(blob);
      const a = document.createElement('a');
      a.href = url;
      a.download = `config-${tagName}.yaml`;
      a.click();
      URL.revokeObjectURL(url);

      setSuccess(`已拉取标签 "${tagName}" 对应的配置`);
    } catch (err) {
      setError('拉取配置失败: ' + err.message);
    }
  };

  const formatDate = (timestamp) => {
    return new Date(timestamp).toLocaleString('zh-CN');
  };

  return (
    <div className="page-container">
      <div style={{ display: 'flex', justifyContent: 'space-between', alignItems: 'center', marginBottom: '24px' }}>
        <h2 className="page-title" style={{ marginBottom: 0, paddingBottom: 0, borderBottom: 'none' }}>
          标签管理
        </h2>
        <button
          className="btn btn-primary"
          onClick={() => setShowCreateModal(true)}
        >
          + 创建标签
        </button>
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
        <div className="tag-list">
          {tags.length === 0 ? (
            <div className="alert alert-info">暂无标签，点击上方按钮创建</div>
          ) : (
            tags.map((tag) => (
              <div key={tag.name} className="tag-card">
                <div style={{ display: 'flex', justifyContent: 'space-between', alignItems: 'center' }}>
                  <h3 style={{ fontSize: '18px', fontWeight: 600 }}>
                    🏷 {tag.name}
                  </h3>
                </div>
                <div className="card-content" style={{ marginTop: '8px' }}>
                  {tag.message && <p><strong>说明:</strong> {tag.message}</p>}
                  <p><strong>提交ID:</strong></p>
                  <code style={{ fontSize: '12px', color: '#6b7280' }}>
                    {tag.commit_id?.substring(0, 20)}...
                  </code>
                  <p style={{ marginTop: '8px' }}><strong>创建时间:</strong> {formatDate(tag.timestamp)}</p>
                </div>
                <div className="tag-actions">
                  <button
                    className="btn btn-primary btn-sm"
                    onClick={() => handlePullByTag(tag.name)}
                  >
                    拉取配置
                  </button>
                  <button
                    className="btn btn-danger btn-sm"
                    onClick={() => handleDeleteTag(tag.name)}
                  >
                    删除
                  </button>
                </div>
              </div>
            ))
          )}
        </div>
      )}

      {showCreateModal && (
        <div className="modal-overlay" onClick={() => setShowCreateModal(false)}>
          <div className="modal" onClick={(e) => e.stopPropagation()}>
            <h3 className="modal-title">创建标签</h3>

            <div className="form-group">
              <label className="form-label">标签名称</label>
              <input
                type="text"
                className="form-input"
                value={newTag.name}
                onChange={(e) => setNewTag({ ...newTag, name: e.target.value })}
                placeholder="例如: v1.0.0"
              />
            </div>

            <div className="form-group">
              <label className="form-label">选择分支</label>
              <select
                className="form-select"
                value={selectedBranch}
                onChange={(e) => setSelectedBranch(e.target.value)}
              >
                {branches.map((b) => (
                  <option key={b.name} value={b.name}>
                    {b.name}
                  </option>
                ))}
              </select>
            </div>

            <div className="form-group">
              <label className="form-label">选择提交</label>
              <select
                className="form-select"
                value={newTag.commit_id}
                onChange={(e) => setNewTag({ ...newTag, commit_id: e.target.value })}
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
              <label className="form-label">标签说明</label>
              <input
                type="text"
                className="form-input"
                value={newTag.message}
                onChange={(e) => setNewTag({ ...newTag, message: e.target.value })}
                placeholder="请输入标签说明（可选）"
              />
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
                onClick={handleCreateTag}
              >
                创建
              </button>
            </div>
          </div>
        </div>
      )}
    </div>
  );
}

export default TagPage;
