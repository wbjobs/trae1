import { useEffect, useState } from 'react';
import { dependenciesApi, versionsApi, componentsApi } from '../services/api';
import type { Component, ComponentVersion, ConflictInfo } from '../types';

export default function DependenciesPage() {
  const [components, setComponents] = useState<Component[]>([]);
  const [selected, setSelected] = useState<Record<string, string[]>>({});
  const [conflicts, setConflicts] = useState<ConflictInfo[]>([]);
  const [analyzing, setAnalyzing] = useState(false);
  const [error, setError] = useState('');

  const load = () => {
    componentsApi
      .list()
      .then((list: Component[]) => {
        setComponents(list);
        const init: Record<string, string[]> = {};
        list.forEach((c) => { init[c.id] = []; });
        setSelected(init);
      })
      .catch((err: unknown) => setError(err instanceof Error ? err.message : String(err)));
  };

  useEffect(() => { load(); }, []);

  const toggleVersion = (componentId: string, versionId: string) => {
    setSelected((prev) => {
      const list = prev[componentId] ?? [];
      const has = list.includes(versionId);
      return {
        ...prev,
        [componentId]: has ? list.filter((v) => v !== versionId) : [...list, versionId],
      };
    });
  };

  const allSelectedIds: string[] = Object.values(selected).flat();

  const analyze = () => {
    if (allSelectedIds.length < 2) {
      setError('请至少选择 2 个版本进行冲突检测');
      setConflicts([]);
      return;
    }
    setError('');
    setAnalyzing(true);
    dependenciesApi
      .analyze(allSelectedIds)
      .then((result: ConflictInfo[]) => setConflicts(result))
      .catch((err: unknown) => setError(err instanceof Error ? err.message : String(err)))
      .finally(() => setAnalyzing(false));
  };

  return (
    <div>
      <div className="page-header">
        <h2>依赖冲突检测</h2>
        <button className="btn" onClick={analyze} disabled={analyzing || allSelectedIds.length < 2}>
          {analyzing ? '检测中...' : `开始检测 (已选 ${allSelectedIds.length} 个版本)`}
        </button>
      </div>

      {error && <div className="card"><p className="conflict">{error}</p></div>}

      <div className="grid grid-2">
        <div className="card">
          <h3>选择版本组合</h3>
          {components.length === 0 ? (
            <p className="empty">暂无组件</p>
          ) : (
            components.map((c) => (
              <div key={c.id} style={{ marginBottom: 12 }}>
                <p><strong>{c.name}</strong></p>
                {(c.versions ?? []).map((v) => (
                  <label key={v.id} style={{ display: 'inline-block', marginRight: 8, marginBottom: 4 }}>
                    <input
                      type="checkbox"
                      checked={selected[c.id]?.includes(v.id) ?? false}
                      onChange={() => toggleVersion(c.id, v.id)}
                    />
                    {' '}
                    <span className={`tag ${v.tag}`}>{v.version}</span>
                    {v.isLatest && <span className="tag latest">LATEST</span>}
                  </label>
                ))}
              </div>
            ))
          )}
        </div>

        <div className="card">
          <h3>检测结果</h3>
          {analyzing ? (
            <p className="empty">检测中...</p>
          ) : conflicts.length === 0 ? (
            <p className="empty">
              {allSelectedIds.length < 2
                ? '请选择至少 2 个版本进行检测'
                : '未检测到依赖冲突'}
            </p>
          ) : (
            <div>
              {conflicts.map((c) => (
                <div key={c.dependencyName} style={{ marginBottom: 12, padding: 12, border: '1px solid #fee2e2', borderRadius: 6 }}>
                  <p className="conflict"><strong>冲突依赖：</strong>{c.dependencyName}</p>
                  <ul>
                    {c.conflicts.map((cf, i) => (
                      <li key={i}>
                        <strong>{cf.componentName}</strong> @ {cf.componentVersion}
                        {' → '}
                        需要 <code>{cf.requiredVersion}</code>
                      </li>
                    ))}
                  </ul>
                  {c.suggestion && <p className="safe"><strong>建议：</strong>{c.suggestion}</p>}
                </div>
              ))}
            </div>
          )}
        </div>
      </div>
    </div>
  );
}
