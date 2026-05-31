import React, { useState } from 'react';
import { configApi, branchApi } from '../services/api';

function UploadPage() {
  const [file, setFile] = useState(null);
  const [branch, setBranch] = useState('main');
  const [message, setMessage] = useState('');
  const [author, setAuthor] = useState('');
  const [branches, setBranches] = useState([]);
  const [loading, setLoading] = useState(false);
  const [result, setResult] = useState(null);
  const [error, setError] = useState(null);
  const [dragActive, setDragActive] = useState(false);

  React.useEffect(() => {
    fetchBranches();
  }, []);

  const fetchBranches = async () => {
    try {
      const res = await branchApi.list();
      setBranches(res.data);
    } catch (err) {
      console.error('Failed to fetch branches:', err);
    }
  };

  const handleDrag = (e) => {
    e.preventDefault();
    e.stopPropagation();
    if (e.type === 'dragenter' || e.type === 'dragover') {
      setDragActive(true);
    } else if (e.type === 'dragleave') {
      setDragActive(false);
    }
  };

  const handleDrop = (e) => {
    e.preventDefault();
    e.stopPropagation();
    setDragActive(false);
    if (e.dataTransfer.files && e.dataTransfer.files[0]) {
      handleFile(e.dataTransfer.files[0]);
    }
  };

  const handleFileChange = (e) => {
    if (e.target.files && e.target.files[0]) {
      handleFile(e.target.files[0]);
    }
  };

  const handleFile = (f) => {
    const validTypes = ['.yaml', '.yml', '.json', '.toml'];
    const fileName = f.name.toLowerCase();
    const isValid = validTypes.some((ext) => fileName.endsWith(ext));
    if (!isValid) {
      setError('仅支持 YAML、JSON 和 TOML 格式的配置文件');
      return;
    }
    setFile(f);
    setError(null);
  };

  const handleUpload = async () => {
    if (!file) {
      setError('请选择要上传的配置文件');
      return;
    }

    setLoading(true);
    setError(null);
    setResult(null);

    try {
      const formData = new FormData();
      formData.append('file', file);

      const res = await configApi.upload(formData, branch, message || 'upload config', author || 'anonymous');
      setResult(res.data);
    } catch (err) {
      setError(err.response?.data?.error || '上传失败，请重试');
    } finally {
      setLoading(false);
    }
  };

  return (
    <div className="page-container">
      <h2 className="page-title">上传配置文件</h2>

      <div
        className={`file-drop-zone ${dragActive ? 'dragging' : ''}`}
        onDragEnter={handleDrag}
        onDragLeave={handleDrag}
        onDragOver={handleDrag}
        onDrop={handleDrop}
        onClick={() => document.getElementById('file-input')?.click()}
      >
        <div className="file-icon">📁</div>
        {file ? (
          <div>
            <p><strong>{file.name}</strong></p>
            <p>{(file.size / 1024).toFixed(2)} KB</p>
          </div>
        ) : (
          <p>拖拽文件到此处或点击选择文件（支持 YAML、JSON、TOML 格式）</p>
        )}
        <input
          id="file-input"
          type="file"
          accept=".yaml,.yml,.json,.toml"
          onChange={handleFileChange}
          style={{ display: 'none' }}
        />
      </div>

      <div className="form-group" style={{ marginTop: '24px' }}>
        <label className="form-label">目标分支</label>
        <select
          className="form-select"
          value={branch}
          onChange={(e) => setBranch(e.target.value)}
        >
          <option value="main">main</option>
          {branches
            .filter((b) => b.name !== 'main')
            .map((b) => (
              <option key={b.name} value={b.name}>
                {b.name}
              </option>
            ))}
        </select>
      </div>

      <div className="form-group">
        <label className="form-label">提交信息</label>
        <input
          type="text"
          className="form-input"
          value={message}
          onChange={(e) => setMessage(e.target.value)}
          placeholder="请输入提交信息（可选）"
        />
      </div>

      <div className="form-group">
        <label className="form-label">作者</label>
        <input
          type="text"
          className="form-input"
          value={author}
          onChange={(e) => setAuthor(e.target.value)}
          placeholder="请输入作者名称（可选）"
        />
      </div>

      {error && (
        <div className="alert alert-error">{error}</div>
      )}

      {result && (
        <div className="alert alert-success">
          <p>✅ 上传成功！</p>
          <p><strong>Commit ID:</strong> {result.commit.id}</p>
          <p><strong>CID:</strong> <span className="badge badge-primary">{result.cid}</span></p>
        </div>
      )}

      <button
        className="btn btn-primary"
        onClick={handleUpload}
        disabled={loading || !file}
        style={{ marginTop: '16px' }}
      >
        {loading ? (
          <>
            <span className="spinner" style={{ display: 'inline-block', width: '16px', height: '16px', marginRight: '8px', verticalAlign: 'middle' }}></span>
            上传中...
          </>
        ) : (
          '上传配置'
        )}
      </button>
    </div>
  );
}

export default UploadPage;
