import React, { useState, useEffect } from 'react';
import { commitApi, branchApi, tagApi } from '../services/api';

function HistoryPage() {
  const [branch, setBranch] = useState('main');
  const [branches, setBranches] = useState([]);
  const [commits, setCommits] = useState([]);
  const [tags, setTags] = useState([]);
  const [selectedCommit, setSelectedCommit] = useState(null);
  const [commitContent, setCommitContent] = useState(null);
  const [loading, setLoading] = useState(false);

  useEffect(() => {
    fetchBranches();
    fetchTags();
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

  const fetchTags = async () => {
    try {
      const res = await tagApi.list();
      setTags(res.data);
    } catch (err) {
      console.error('Failed to fetch tags:', err);
    }
  };

  const fetchCommits = async () => {
    setLoading(true);
    try {
      const res = await commitApi.list(branch);
      setCommits(res.data);
    } catch (err) {
      console.error('Failed to fetch commits:', err);
    } finally {
      setLoading(false);
    }
  };

  const handleViewCommit = async (commitId) => {
    try {
      const res = await commitApi.get(commitId);
      setSelectedCommit(res.data);

      const contentRes = await fetch(
        `http://localhost:8080/api/configs/commit/${commitId}/content`
      );
      const content = await contentRes.text();
      setCommitContent(content);
    } catch (err) {
      console.error('Failed to fetch commit:', err);
    }
  };

  const getTagsForCommit = (commitId) => {
    return tags.filter((t) => t.commit_id === commitId);
  };

  const formatDate = (timestamp) => {
    return new Date(timestamp).toLocaleString('zh-CN');
  };

  return (
    <div className="page-container">
      <h2 className="page-title">版本历史</h2>

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

      {loading ? (
        <div className="loading">
          <div className="spinner"></div>
          加载中...
        </div>
      ) : (
        <div className="commit-list">
          {commits.length === 0 ? (
            <div className="alert alert-info">该分支暂无提交记录</div>
          ) : (
            commits.map((commit) => (
              <div key={commit.id} className="commit-item">
                <div className="commit-header">
                  <span className="commit-message">{commit.message}</span>
                  <span className="commit-cid" title={commit.cid}>
                    {commit.cid.substring(0, 12)}...
                  </span>
                </div>
                <div className="commit-meta">
                  <span>作者: {commit.author}</span>
                  <span>时间: {formatDate(commit.timestamp)}</span>
                  <span>分支: <span className="badge badge-primary">{commit.branch}</span></span>
                  {getTagsForCommit(commit.id).map((tag) => (
                    <span key={tag.name} className="badge badge-success">
                      🏷 {tag.name}
                    </span>
                  ))}
                </div>
                <div style={{ marginTop: '12px' }}>
                  <button
                    className="btn btn-secondary btn-sm"
                    onClick={() => handleViewCommit(commit.id)}
                  >
                    查看详情
                  </button>
                </div>
              </div>
            ))
          )}
        </div>
      )}

      {selectedCommit && (
        <div className="modal-overlay" onClick={() => { setSelectedCommit(null); setCommitContent(null); }}>
          <div className="modal" onClick={(e) => e.stopPropagation()}>
            <h3 className="modal-title">提交详情</h3>
            <div className="card">
              <div className="card-content">
                <p><strong>Commit ID:</strong></p>
                <code style={{ wordBreak: 'break-all' }}>{selectedCommit.id}</code>
                <p style={{ marginTop: '12px' }}><strong>CID:</strong></p>
                <code style={{ wordBreak: 'break-all' }}>{selectedCommit.cid}</code>
                <p style={{ marginTop: '12px' }}><strong>消息:</strong> {selectedCommit.message}</p>
                <p><strong>作者:</strong> {selectedCommit.author}</p>
                <p><strong>时间:</strong> {formatDate(selectedCommit.timestamp)}</p>
                <p><strong>分支:</strong> {selectedCommit.branch}</p>
                {selectedCommit.parent_id && (
                  <p><strong>父提交:</strong> <code>{selectedCommit.parent_id.substring(0, 12)}...</code></p>
                )}
              </div>
            </div>

            {commitContent && (
              <>
                <h4 style={{ marginTop: '20px' }}>配置内容:</h4>
                <pre style={{
                  background: '#1e1e1e',
                  color: '#d4d4d4',
                  padding: '16px',
                  borderRadius: '8px',
                  maxHeight: '300px',
                  overflow: 'auto',
                  fontSize: '13px',
                }}>
                  {commitContent}
                </pre>
              </>
            )}

            <div className="modal-actions">
              <button
                className="btn btn-secondary"
                onClick={() => { setSelectedCommit(null); setCommitContent(null); }}
              >
                关闭
              </button>
            </div>
          </div>
        </div>
      )}
    </div>
  );
}

export default HistoryPage;
