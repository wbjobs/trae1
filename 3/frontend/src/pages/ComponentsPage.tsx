import { useEffect, useState } from 'react';
import { useNavigate } from 'react-router-dom';
import { componentsApi } from '../services/api';
import { useComponentStore } from '../stores/component.store';
import type { Component, CreateComponentInput } from '../types';

export default function ComponentsPage() {
  const navigate = useNavigate();
  const { components, setComponents, loading, setLoading } = useComponentStore();
  const [showForm, setShowForm] = useState(false);
  const [form, setForm] = useState<CreateComponentInput>({ name: '', description: '', category: '' });
  const [error, setError] = useState('');
  const [sortBy, setSortBy] = useState('createdAt');
  const [topList, setTopList] = useState<Component[]>([]);

  const load = () => {
    setLoading(true);
    Promise.all([
      componentsApi.list(undefined, sortBy),
      componentsApi.top(5),
    ])
      .then(([list, top]) => {
        setComponents(list as Component[]);
        setTopList(top as Component[]);
      })
      .catch((err: unknown) => setError(err instanceof Error ? err.message : String(err)))
      .finally(() => setLoading(false));
  };

  useEffect(() => { load(); }, [sortBy]);

  const handleSubmit = (e: React.FormEvent) => {
    e.preventDefault();
    setError('');
    componentsApi
      .create(form)
      .then(() => {
        setShowForm(false);
        setForm({ name: '', description: '', category: '' });
        load();
      })
      .catch((err: unknown) => setError(err instanceof Error ? err.message : String(err)));
  };

  const handleDelete = (id: string) => {
    if (!confirm('确定删除该组件及其所有版本？')) return;
    componentsApi
      .remove(id)
      .then(load)
      .catch((err: unknown) => setError(err instanceof Error ? err.message : String(err)));
  };

  return (
    <div>
      <div className="page-header">
        <h2>组件管理</h2>
        <div style={{ display: 'flex', gap: 8 }}>
          <select
            value={sortBy}
            onChange={(e) => setSortBy(e.target.value)}
            style={{ padding: '6px 10px', borderRadius: 6, border: '1px solid #d1d5db' }}
          >
            <option value="createdAt">按创建时间</option>
            <option value="popularity">按热度</option>
          </select>
          <button className="btn" onClick={() => setShowForm(!showForm)}>
            {showForm ? '取消' : '新建组件'}
          </button>
        </div>
      </div>

      {error && (
        <div className="card"><p className="conflict">{error}</p></div>
      )}

      {topList.length > 0 && (
        <div className="card">
          <h3>🔥 热度 Top 5</h3>
          <table>
            <thead>
              <tr><th>排名</th><th>组件</th><th>热度分</th><th>下载</th><th>预览</th><th>引用</th></tr>
            </thead>
            <tbody>
              {topList.map((c, i) => (
                <tr key={c.id}>
                  <td>
                    <span style={{
                      display: 'inline-block',
                      width: 24, height: 24, lineHeight: '24px',
                      textAlign: 'center', borderRadius: '50%',
                      background: i < 3 ? '#fef3c7' : '#f3f4f6',
                      fontWeight: 'bold',
                    }}>{i + 1}</span>
                  </td>
                  <td>
                    <a onClick={() => navigate(`/docs/${c.name}`)} style={{ cursor: 'pointer', color: '#2563eb' }}>
                      <strong>{c.name}</strong>
                    </a>
                  </td>
                  <td>{c.popularityScore}</td>
                  <td>{c.downloadCount}</td>
                  <td>{c.previewCount}</td>
                  <td>{c.referenceCount}</td>
                </tr>
              ))}
            </tbody>
          </table>
        </div>
      )}

      {showForm && (
        <div className="card">
          <h3>新建组件</h3>
          <form onSubmit={handleSubmit}>
            <div className="form-row">
              <label>名称 *</label>
              <input
                value={form.name}
                onChange={(e) => setForm({ ...form, name: e.target.value })}
                placeholder="例如 @acme/button"
                required
              />
            </div>
            <div className="form-row">
              <label>描述</label>
              <textarea
                value={form.description}
                onChange={(e) => setForm({ ...form, description: e.target.value })}
                rows={2}
              />
            </div>
            <div className="form-row">
              <label>分类</label>
              <input
                value={form.category}
                onChange={(e) => setForm({ ...form, category: e.target.value })}
                placeholder="basic / form / layout ..."
              />
            </div>
            <button className="btn" type="submit" disabled={!form.name.trim()}>创建</button>
          </form>
        </div>
      )}

      <div className="card">
        {loading ? (
          <p className="empty">加载中...</p>
        ) : components.length === 0 ? (
          <p className="empty">暂无组件，点击"新建组件"开始</p>
        ) : (
          <table>
            <thead>
              <tr>
                <th>名称</th>
                <th>描述</th>
                <th>分类</th>
                <th>状态</th>
                <th>版本数</th>
                <th>热度</th>
                <th>创建时间</th>
                <th>操作</th>
              </tr>
            </thead>
            <tbody>
              {components.map((c) => (
                <tr key={c.id}>
                  <td><strong>{c.name}</strong></td>
                  <td>{c.description ?? '-'}</td>
                  <td>{c.category ?? '-'}</td>
                  <td>
                    <span className={`tag ${c.status}`}>{c.status}</span>
                  </td>
                  <td>{c.versions?.length ?? 0}</td>
                  <td>{c.popularityScore}</td>
                  <td>{new Date(c.createdAt).toLocaleDateString()}</td>
                  <td>
                    <button className="btn small" onClick={() => navigate(`/versions/${c.id}`)}>
                      版本
                    </button>
                    <button className="btn small secondary" onClick={() => navigate(`/docs/${c.name}`)} style={{ marginLeft: 4 }}>
                      文档
                    </button>
                    <button className="btn small danger" onClick={() => handleDelete(c.id)} style={{ marginLeft: 4 }}>
                      删除
                    </button>
                  </td>
                </tr>
              ))}
            </tbody>
          </table>
        )}
      </div>
    </div>
  );
}
