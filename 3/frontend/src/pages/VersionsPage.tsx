import { useEffect, useState } from 'react';
import { useNavigate, useParams } from 'react-router-dom';
import { versionsApi } from '../services/api';
import type { ComponentVersion, CreateVersionInput } from '../types';

export default function VersionsPage() {
  const { componentId } = useParams<{ componentId: string }>();
  const navigate = useNavigate();
  const [versions, setVersions] = useState<ComponentVersion[]>([]);
  const [loading, setLoading] = useState(false);
  const [error, setError] = useState('');
  const [showForm, setShowForm] = useState(false);
  const [form, setForm] = useState<CreateVersionInput>({
    componentId: componentId ?? '',
    version: '',
    changelog: '',
    exports: [],
    tag: 'stable',
    isLatest: true,
    dependencies: [],
  });

  const load = () => {
    if (!componentId) return;
    setLoading(true);
    versionsApi
      .listByComponent(componentId)
      .then((list: ComponentVersion[]) => setVersions(list))
      .catch((err: unknown) => setError(err instanceof Error ? err.message : String(err)))
      .finally(() => setLoading(false));
  };

  useEffect(() => { load(); }, [componentId]);

  const suggestNext = (bump: 'major' | 'minor' | 'patch') => {
    if (!componentId) return;
    versionsApi
      .suggest(componentId, bump)
      .then((v: string) => setForm({ ...form, version: v }))
      .catch((err: unknown) => setError(err instanceof Error ? err.message : String(err)));
  };

  const handleSubmit = (e: React.FormEvent) => {
    e.preventDefault();
    setError('');
    const payload = { ...form };
    if (typeof form.exports === 'string') {
      payload.exports = (form.exports as unknown as string).split(',').map((s) => s.trim()).filter(Boolean);
    }
    versionsApi
      .create(payload)
      .then(() => {
        setShowForm(false);
        setForm({
          componentId: componentId ?? '',
          version: '',
          changelog: '',
          exports: [],
          tag: 'stable',
          isLatest: true,
          dependencies: [],
        });
        load();
      })
      .catch((err: unknown) => setError(err instanceof Error ? err.message : String(err)));
  };

  const handleSetLatest = (id: string) => {
    versionsApi
      .setLatest(id)
      .then(load)
      .catch((err: unknown) => setError(err instanceof Error ? err.message : String(err)));
  };

  const handleRollback = (id: string) => {
    if (!confirm('确定将该版本标记为最新（软回滚）？')) return;
    versionsApi
      .rollback(id)
      .then(load)
      .catch((err: unknown) => setError(err instanceof Error ? err.message : String(err)));
  };

  const handleRollbackClone = (id: string) => {
    const bump = prompt('请输入版本号递增类型 (major/minor/patch)：', 'patch') as 'major' | 'minor' | 'patch';
    if (!bump) return;
    versionsApi
      .rollbackClone(id, bump)
      .then(load)
      .catch((err: unknown) => setError(err instanceof Error ? err.message : String(err)));
  };

  const handleDelete = (id: string) => {
    if (!confirm('确定删除该版本？')) return;
    versionsApi
      .remove(id)
      .then(load)
      .catch((err: unknown) => setError(err instanceof Error ? err.message : String(err)));
  };

  return (
    <div>
      <div className="page-header">
        <h2>版本管理</h2>
        <div>
          <button className="btn" onClick={() => setShowForm(!showForm)}>
            {showForm ? '取消' : '发布新版本'}
          </button>
        </div>
      </div>

      {error && <div className="card"><p className="conflict">{error}</p></div>}

      {showForm && (
        <div className="card">
          <h3>发布新版本</h3>
          <form onSubmit={handleSubmit}>
            <div className="form-row">
              <label>版本号 *</label>
              <div style={{ display: 'flex', gap: 4 }}>
                <input
                  value={form.version}
                  onChange={(e) => setForm({ ...form, version: e.target.value })}
                  placeholder="1.0.0"
                  required
                  style={{ flex: 1 }}
                />
                <button type="button" className="btn small secondary" onClick={() => suggestNext('patch')}>+patch</button>
                <button type="button" className="btn small secondary" onClick={() => suggestNext('minor')}>+minor</button>
                <button type="button" className="btn small secondary" onClick={() => suggestNext('major')}>+major</button>
              </div>
            </div>
            <div className="form-row">
              <label>更新日志</label>
              <textarea
                value={form.changelog}
                onChange={(e) => setForm({ ...form, changelog: e.target.value })}
                rows={3}
              />
            </div>
            <div className="form-row">
              <label>导出成员 (逗号分隔)</label>
              <input
                value={Array.isArray(form.exports) ? form.exports.join(', ') : form.exports}
                onChange={(e) => setForm({ ...form, exports: e.target.value.split(',').map((s) => s.trim()).filter(Boolean) })}
                placeholder="Button, Input, Modal"
              />
            </div>
            <div className="form-row">
              <label>标签</label>
              <select
                value={form.tag}
                onChange={(e) => setForm({ ...form, tag: e.target.value as CreateVersionInput['tag'] })}
              >
                <option value="alpha">alpha</option>
                <option value="beta">beta</option>
                <option value="rc">rc</option>
                <option value="stable">stable</option>
                <option value="deprecated">deprecated</option>
              </select>
            </div>
            <div className="form-row">
              <label>
                <input
                  type="checkbox"
                  checked={form.isLatest}
                  onChange={(e) => setForm({ ...form, isLatest: e.target.checked })}
                />
                {' '}设为最新版本
              </label>
            </div>
            <button className="btn" type="submit" disabled={!form.version.trim()}>发布</button>
          </form>
        </div>
      )}

      <div className="card">
        {loading ? (
          <p className="empty">加载中...</p>
        ) : versions.length === 0 ? (
          <p className="empty">暂无版本</p>
        ) : (
          <table>
            <thead>
              <tr>
                <th>版本</th>
                <th>标签</th>
                <th>最新</th>
                <th>更新日志</th>
                <th>创建时间</th>
                <th>操作</th>
              </tr>
            </thead>
            <tbody>
              {versions.map((v) => (
                <tr key={v.id}>
                  <td><strong>{v.version}</strong></td>
                  <td><span className={`tag ${v.tag}`}>{v.tag}</span></td>
                  <td>{v.isLatest && <span className="tag latest">LATEST</span>}</td>
                  <td>{v.changelog ?? '-'}</td>
                  <td>{new Date(v.createdAt).toLocaleDateString()}</td>
                  <td>
                    <button className="btn small" onClick={() => navigate(`/preview/${v.id}`)}>预览</button>
                    <button className="btn small secondary" onClick={() => navigate(`/bundle/${v.id}`)} style={{ marginLeft: 4 }}>
                      体积
                    </button>
                    {!v.isLatest && (
                      <button className="btn small secondary" onClick={() => handleSetLatest(v.id)} style={{ marginLeft: 4 }}>
                        设为最新
                      </button>
                    )}
                    <button className="btn small secondary" onClick={() => handleRollback(v.id)} style={{ marginLeft: 4 }} title="将该版本标记为最新">
                      回滚
                    </button>
                    <button className="btn small secondary" onClick={() => handleRollbackClone(v.id)} style={{ marginLeft: 4 }} title="克隆该版本并递增版本号">
                      回滚(克隆)
                    </button>
                    <button className="btn small danger" onClick={() => handleDelete(v.id)} style={{ marginLeft: 4 }}>
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
