import React, { useState, useEffect } from 'react';
import { mergeApi } from '../services/api';

function MergeStrategyConfig({ strategy, onChange, readOnly = false }) {
  const [localStrategy, setLocalStrategy] = useState(strategy || {
    array_merge_by_keys: {},
    prefer_source_paths: [],
    prefer_target_paths: [],
    combine_arrays_paths: [],
  });
  const [newKeyPath, setNewKeyPath] = useState('');
  const [newKeyFields, setNewKeyFields] = useState('name');
  const [newPreferPath, setNewPreferPath] = useState('');
  const [newCombinePath, setNewCombinePath] = useState('');

  useEffect(() => {
    if (strategy) {
      setLocalStrategy(strategy);
    }
  }, [strategy]);

  useEffect(() => {
    if (onChange) {
      onChange(localStrategy);
    }
  }, [localStrategy, onChange]);

  useEffect(() => {
    const fetchDefault = async () => {
      try {
        const res = await mergeApi.getDefaultStrategy();
        if (!strategy) {
          setLocalStrategy(res.data);
        }
      } catch (err) {
        console.error('Failed to fetch default strategy:', err);
      }
    };
    if (!strategy) {
      fetchDefault();
    }
  }, [strategy]);

  const addArrayMergeKey = () => {
    if (!newKeyPath.trim()) return;
    const fields = newKeyFields.split(',').map(f => f.trim()).filter(f => f);
    setLocalStrategy(prev => ({
      ...prev,
      array_merge_by_keys: {
        ...prev.array_merge_by_keys,
        [newKeyPath.trim()]: fields,
      },
    }));
    setNewKeyPath('');
    setNewKeyFields('name');
  };

  const removeArrayMergeKey = (path) => {
    setLocalStrategy(prev => {
      const next = { ...prev, array_merge_by_keys: { ...prev.array_merge_by_keys } };
      delete next.array_merge_by_keys[path];
      return next;
    });
  };

  const addPreferPath = (type) => {
    if (!newPreferPath.trim()) return;
    const key = type === 'source' ? 'prefer_source_paths' : 'prefer_target_paths';
    setLocalStrategy(prev => ({
      ...prev,
      [key]: [...new Set([...prev[key], newPreferPath.trim()])],
    }));
    setNewPreferPath('');
  };

  const removePreferPath = (type, path) => {
    const key = type === 'source' ? 'prefer_source_paths' : 'prefer_target_paths';
    setLocalStrategy(prev => ({
      ...prev,
      [key]: prev[key].filter(p => p !== path),
    }));
  };

  const addCombinePath = () => {
    if (!newCombinePath.trim()) return;
    setLocalStrategy(prev => ({
      ...prev,
      combine_arrays_paths: [...new Set([...prev.combine_arrays_paths, newCombinePath.trim()])],
    }));
    setNewCombinePath('');
  };

  const removeCombinePath = (path) => {
    setLocalStrategy(prev => ({
      ...prev,
      combine_arrays_paths: prev.combine_arrays_paths.filter(p => p !== path),
    }));
  };

  const resetToDefault = async () => {
    try {
      const res = await mergeApi.getDefaultStrategy();
      setLocalStrategy(res.data);
    } catch (err) {
      console.error('Failed to fetch default strategy:', err);
    }
  };

  return (
    <div style={{ border: '1px solid #e5e7eb', borderRadius: '12px', padding: '16px', background: '#fafafa' }}>
      <div style={{ display: 'flex', justifyContent: 'space-between', alignItems: 'center', marginBottom: '16px' }}>
        <h3 style={{ fontSize: '16px', fontWeight: 600, margin: 0 }}>⚙️ 合并策略配置</h3>
        {!readOnly && (
          <button className="btn btn-secondary btn-sm" onClick={resetToDefault}>
            重置为默认
          </button>
        )}
      </div>

      <div style={{ marginBottom: '20px' }}>
        <h4 style={{ fontSize: '14px', fontWeight: 600, marginBottom: '8px', color: '#374151' }}>
          📋 数组按键去重合并
        </h4>
        <p style={{ fontSize: '12px', color: '#6b7280', marginBottom: '12px' }}>
          对于指定路径下的数组，按指定字段（如name）匹配合并，避免重复
        </p>

        {!readOnly && (
          <div style={{ display: 'flex', gap: '8px', marginBottom: '12px' }}>
            <input
              type="text"
              className="form-input"
              placeholder="路径: .spec.containers"
              value={newKeyPath}
              onChange={(e) => setNewKeyPath(e.target.value)}
              style={{ flex: 1, fontSize: '13px' }}
            />
            <input
              type="text"
              className="form-input"
              placeholder="字段: name"
              value={newKeyFields}
              onChange={(e) => setNewKeyFields(e.target.value)}
              style={{ width: '150px', fontSize: '13px' }}
            />
            <button className="btn btn-primary btn-sm" onClick={addArrayMergeKey}>
              添加
            </button>
          </div>
        )}

        <div style={{ display: 'flex', flexDirection: 'column', gap: '6px' }}>
          {Object.entries(localStrategy.array_merge_by_keys || {}).map(([path, fields]) => (
            <div
              key={path}
              style={{
                display: 'flex',
                justifyContent: 'space-between',
                alignItems: 'center',
                padding: '8px 12px',
                background: 'white',
                borderRadius: '6px',
                border: '1px solid #e5e7eb',
                fontSize: '13px',
              }}
            >
              <div>
                <code style={{ color: '#667eea' }}>{path}</code>
                <span style={{ marginLeft: '12px', color: '#6b7280' }}>
                  键: {fields.join(', ')}
                </span>
              </div>
              {!readOnly && (
                <button
                  className="btn btn-danger btn-sm"
                  onClick={() => removeArrayMergeKey(path)}
                >
                  删除
                </button>
              )}
            </div>
          ))}
          {Object.keys(localStrategy.array_merge_by_keys || {}).length === 0 && (
            <div style={{ fontSize: '13px', color: '#9ca3af', textAlign: 'center', padding: '12px' }}>
              暂无配置
            </div>
          )}
        </div>
      </div>

      <div style={{ marginBottom: '20px' }}>
        <h4 style={{ fontSize: '14px', fontWeight: 600, marginBottom: '8px', color: '#374151' }}>
          ⚡ 自动优先选择（无冲突时）
        </h4>

        <div style={{ marginBottom: '12px' }}>
          <p style={{ fontSize: '12px', color: '#6b7280', marginBottom: '8px' }}>
            优先源分支 (Source)
          </p>
          {!readOnly && (
            <div style={{ display: 'flex', gap: '8px', marginBottom: '8px' }}>
              <input
                type="text"
                className="form-input"
                placeholder="路径: .spec.replicas"
                value={newPreferPath}
                onChange={(e) => setNewPreferPath(e.target.value)}
                style={{ flex: 1, fontSize: '13px' }}
              />
              <button className="btn btn-success btn-sm" onClick={() => addPreferPath('source')}>
                添加到源分支
              </button>
              <button className="btn btn-primary btn-sm" onClick={() => addPreferPath('target')}>
                添加到目标分支
              </button>
            </div>
          )}

          <div style={{ display: 'flex', flexDirection: 'column', gap: '4px' }}>
            {(localStrategy.prefer_source_paths || []).map(path => (
              <div
                key={`s-${path}`}
                style={{
                  display: 'flex',
                  justifyContent: 'space-between',
                  alignItems: 'center',
                  padding: '6px 10px',
                  background: '#dcfce7',
                  borderRadius: '4px',
                  fontSize: '12px',
                }}
              >
                <div>
                  <span className="badge badge-success">源分支</span>
                  <code style={{ marginLeft: '8px', color: '#15803d' }}>{path}</code>
                </div>
                {!readOnly && (
                  <button
                    className="btn btn-danger btn-sm"
                    onClick={() => removePreferPath('source', path)}
                  >
                    删除
                  </button>
                )}
              </div>
            ))}
            {(localStrategy.prefer_target_paths || []).map(path => (
              <div
                key={`t-${path}`}
                style={{
                  display: 'flex',
                  justifyContent: 'space-between',
                  alignItems: 'center',
                  padding: '6px 10px',
                  background: '#dbeafe',
                  borderRadius: '4px',
                  fontSize: '12px',
                }}
              >
                <div>
                  <span className="badge badge-primary">目标分支</span>
                  <code style={{ marginLeft: '8px', color: '#1d4ed8' }}>{path}</code>
                </div>
                {!readOnly && (
                  <button
                    className="btn btn-danger btn-sm"
                    onClick={() => removePreferPath('target', path)}
                  >
                    删除
                  </button>
                )}
              </div>
            ))}
          </div>
        </div>
      </div>

      <div>
        <h4 style={{ fontSize: '14px', fontWeight: 600, marginBottom: '8px', color: '#374151' }}>
          ➕ 数组合并策略（合并两个数组）
        </h4>
        <p style={{ fontSize: '12px', color: '#6b7280', marginBottom: '12px' }}>
          对于指定路径的数组，合并时将源和目标的元素组合在一起（不去重）
        </p>

        {!readOnly && (
          <div style={{ display: 'flex', gap: '8px', marginBottom: '12px' }}>
            <input
              type="text"
              className="form-input"
              placeholder="路径: .spec.ports"
              value={newCombinePath}
              onChange={(e) => setNewCombinePath(e.target.value)}
              style={{ flex: 1, fontSize: '13px' }}
            />
            <button className="btn btn-primary btn-sm" onClick={addCombinePath}>
              添加
            </button>
          </div>
        )}

        <div style={{ display: 'flex', flexDirection: 'column', gap: '4px' }}>
          {(localStrategy.combine_arrays_paths || []).map(path => (
            <div
              key={path}
              style={{
                display: 'flex',
                justifyContent: 'space-between',
                alignItems: 'center',
                padding: '6px 10px',
                background: '#fef3c7',
                borderRadius: '4px',
                fontSize: '12px',
              }}
            >
              <code style={{ color: '#b45309' }}>{path}</code>
              {!readOnly && (
                <button
                  className="btn btn-danger btn-sm"
                  onClick={() => removeCombinePath(path)}
                >
                  删除
                </button>
              )}
            </div>
          ))}
          {(localStrategy.combine_arrays_paths || []).length === 0 && (
            <div style={{ fontSize: '13px', color: '#9ca3af', textAlign: 'center', padding: '12px' }}>
              暂无配置
            </div>
          )}
        </div>
      </div>
    </div>
  );
}

export default MergeStrategyConfig;
