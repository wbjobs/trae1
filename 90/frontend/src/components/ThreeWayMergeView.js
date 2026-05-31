import React, { useState, useEffect, useCallback } from 'react';
import yaml from 'js-yaml';

function ThreeWayMergeView({ mergeResult, onResolve, onCancel }) {
  const [resolutions, setResolutions] = useState({});
  const [conflicts, setConflicts] = useState([]);
  const [selectedConflict, setSelectedConflict] = useState(null);

  useEffect(() => {
    if (mergeResult && mergeResult.merge_result && mergeResult.merge_result.root) {
      const extracted = extractConflicts(mergeResult.merge_result.root);
      setConflicts(extracted);
      if (extracted.length > 0) {
        setSelectedConflict(extracted[0]);
      }
    }
  }, [mergeResult]);

  const extractConflicts = (node, path = '') => {
    let conflicts = [];
    const currentPath = node.path || path;

    if (node.change === 'conflict' && node.conflict) {
      conflicts.push({
        path: currentPath,
        key: node.key || currentPath,
        base: node.conflict.base,
        source: node.conflict.source,
        target: node.conflict.target,
      });
    }

    if (node.children) {
      Object.values(node.children).forEach(child => {
        conflicts = conflicts.concat(extractConflicts(child, currentPath));
      });
    }

    if (node.items) {
      node.items.forEach((item, idx) => {
        conflicts = conflicts.concat(extractConflicts(item, `${currentPath}[${idx}]`));
      });
    }

    return conflicts;
  };

  const handleResolve = useCallback((path, selection) => {
    setResolutions(prev => ({
      ...prev,
      [path]: selection
    }));

    setSelectedConflict(prev => {
      if (prev && prev.path === path) {
        const idx = conflicts.findIndex(c => c.path === path);
        if (idx < conflicts.length - 1) {
          return conflicts[idx + 1];
        }
        return null;
      }
      return prev;
    });
  }, [conflicts]);

  const handleResolveAll = (selection) => {
    const allResolutions = {};
    conflicts.forEach(c => {
      allResolutions[c.path] = selection;
    });
    setResolutions(allResolutions);
    setSelectedConflict(null);
  };

  const handleSubmit = () => {
    const unresolved = conflicts.filter(c => !resolutions[c.path]);
    if (unresolved.length > 0) {
      alert(`还有 ${unresolved.length} 个冲突未解决`);
      return;
    }
    onResolve(resolutions);
  };

  const nodeToYAML = (node) => {
    if (!node) return '';
    const obj = nodeToObject(node);
    try {
      return yaml.dump(obj, { indent: 2 });
    } catch {
      return JSON.stringify(obj, null, 2);
    }
  };

  const nodeToObject = (node) => {
    if (!node) return null;

    switch (node.type) {
      case 'mapping':
        const obj = {};
        if (node.children) {
          Object.entries(node.children).forEach(([key, child]) => {
            obj[key] = nodeToObject(child);
          });
        }
        return obj;

      case 'sequence':
        return node.items ? node.items.map(item => nodeToObject(item)) : [];

      case 'scalar':
        return node.value;

      case 'null':
        return null;

      default:
        return node.value;
    }
  };

  const renderConflictPanel = (title, node, type, path) => {
    const isSelected = resolutions[path] === type;

    return (
      <div
        className={`conflict-panel ${isSelected ? 'selected' : ''} ${type}`}
        onClick={() => handleResolve(path, type)}
        style={{
          flex: 1,
          border: isSelected ? `3px solid ${type === 'source' ? '#4caf50' : type === 'target' ? '#2196f3' : '#ff9800'}` : '1px solid #ddd',
          borderRadius: '8px',
          padding: '12px',
          cursor: 'pointer',
          transition: 'all 0.2s',
          background: isSelected ? (type === 'source' ? '#e8f5e9' : type === 'target' ? '#e3f2fd' : '#fff3e0') : 'white',
        }}
      >
        <div style={{
          display: 'flex',
          justifyContent: 'space-between',
          alignItems: 'center',
          marginBottom: '8px',
          paddingBottom: '8px',
          borderBottom: '1px solid #eee',
        }}>
          <strong style={{
            color: type === 'source' ? '#2e7d32' : type === 'target' ? '#1565c0' : '#e65100',
          }}>
            {title}
          </strong>
          {isSelected && (
            <span className="badge badge-success">✓ 已选择</span>
          )}
        </div>
        <pre style={{
          fontSize: '12px',
          maxHeight: '300px',
          overflow: 'auto',
          background: '#fafafa',
          padding: '8px',
          borderRadius: '4px',
          margin: 0,
          whiteSpace: 'pre-wrap',
          wordBreak: 'break-all',
        }}>
          {nodeToYAML(node)}
        </pre>
      </div>
    );
  };

  const getValuePreview = (node) => {
    if (!node) return 'null';
    if (node.type === 'scalar') return String(node.value);
    if (node.type === 'mapping') return '{...}';
    if (node.type === 'sequence') return `[${node.items ? node.items.length : 0} items]`;
    return 'null';
  };

  if (!mergeResult || !mergeResult.merge_result) {
    return <div className="alert alert-info">没有合并数据</div>;
  }

  return (
    <div className="page-container" style={{ maxWidth: '1600px', margin: '0 auto' }}>
      <div style={{ display: 'flex', justifyContent: 'space-between', alignItems: 'center', marginBottom: '20px' }}>
        <h2 className="page-title" style={{ marginBottom: 0, paddingBottom: 0, borderBottom: 'none' }}>
          🔀 三路合并冲突解决
        </h2>
        <button className="btn btn-secondary" onClick={onCancel}>
          取消
        </button>
      </div>

      <div className="alert alert-info">
        <p>发现 <strong>{conflicts.length}</strong> 个冲突。请逐个选择要保留的版本。</p>
        <p>已解决: <strong>{Object.keys(resolutions).length}</strong> / {conflicts.length}</p>
      </div>

      <div style={{ marginBottom: '16px', display: 'flex', gap: '8px' }}>
        <button
          className="btn btn-secondary btn-sm"
          onClick={() => handleResolveAll('source')}
        >
          全部选择源分支
        </button>
        <button
          className="btn btn-secondary btn-sm"
          onClick={() => handleResolveAll('target')}
        >
          全部选择目标分支
        </button>
        <button
          className="btn btn-secondary btn-sm"
          onClick={() => handleResolveAll('base')}
        >
          全部保留基准版本
        </button>
      </div>

      <div style={{ display: 'flex', gap: '20px', marginBottom: '20px' }}>
        <div style={{ width: '300px', maxHeight: '500px', overflow: 'auto', border: '1px solid #eee', borderRadius: '8px', padding: '8px' }}>
          <h4 style={{ marginBottom: '12px', fontSize: '14px', fontWeight: 600 }}>冲突列表</h4>
          {conflicts.map((conflict, idx) => (
            <div
              key={conflict.path}
              onClick={() => setSelectedConflict(conflict)}
              style={{
                padding: '10px',
                marginBottom: '6px',
                borderRadius: '6px',
                cursor: 'pointer',
                background: selectedConflict?.path === conflict.path ? '#e3f2fd' : resolutions[conflict.path] ? '#e8f5e9' : '#fafafa',
                border: `1px solid ${selectedConflict?.path === conflict.path ? '#2196f3' : resolutions[conflict.path] ? '#4caf50' : '#eee'}`,
                fontSize: '12px',
              }}
            >
              <div style={{ display: 'flex', justifyContent: 'space-between', alignItems: 'center' }}>
                <strong>{conflict.key}</strong>
                {resolutions[conflict.path] && (
                  <span className="badge badge-success" style={{ fontSize: '10px' }}>✓</span>
                )}
              </div>
              <div style={{ fontSize: '11px', color: '#666', marginTop: '4px', wordBreak: 'break-all' }}>
                {conflict.path}
              </div>
              <div style={{ fontSize: '10px', color: '#999', marginTop: '4px' }}>
                源: {getValuePreview(conflict.source)} → 目标: {getValuePreview(conflict.target)}
              </div>
            </div>
          ))}
        </div>

        <div style={{ flex: 1 }}>
          {selectedConflict ? (
            <>
              <div style={{ marginBottom: '12px' }}>
                <h4 style={{ fontSize: '16px', fontWeight: 600 }}>
                  冲突: {selectedConflict.key}
                </h4>
                <p style={{ fontSize: '12px', color: '#666', wordBreak: 'break-all' }}>
                  路径: {selectedConflict.path}
                </p>
              </div>

              <div style={{ display: 'flex', gap: '12px', marginBottom: '16px' }}>
                {renderConflictPanel('源分支 (Source)', selectedConflict.source, 'source', selectedConflict.path)}
                {renderConflictPanel('目标分支 (Target)', selectedConflict.target, 'target', selectedConflict.path)}
              </div>

              <div>
                {renderConflictPanel('基准版本 (Base - 公共祖先)', selectedConflict.base, 'base', selectedConflict.path)}
              </div>

              <div style={{ marginTop: '16px', padding: '12px', background: '#f5f5f5', borderRadius: '8px' }}>
                <h5 style={{ fontSize: '14px', marginBottom: '8px' }}>操作说明</h5>
                <ul style={{ fontSize: '12px', color: '#666', marginLeft: '20px' }}>
                  <li>点击上方任一版本即可选择保留该版本的内容</li>
                  <li>也可使用上方的快捷按钮批量选择</li>
                  <li>所有冲突解决后，点击"完成合并"提交更改</li>
                </ul>
              </div>
            </>
          ) : (
            <div style={{ textAlign: 'center', padding: '40px', color: '#666' }}>
              {Object.keys(resolutions).length === conflicts.length && conflicts.length > 0 ? (
                <>
                  <p style={{ fontSize: '18px', color: '#4caf50', marginBottom: '16px' }}>
                    ✓ 所有冲突已解决！
                  </p>
                  <p>点击下方"完成合并"按钮提交更改。</p>
                </>
              ) : (
                <p>请从左侧选择一个冲突进行解决</p>
              )}
            </div>
          )}
        </div>
      </div>

      <div style={{ display: 'flex', justifyContent: 'space-between', alignItems: 'center', paddingTop: '16px', borderTop: '2px solid #f0f0f0' }}>
        <div>
          <span className="badge badge-primary">
            源分支: {mergeResult.source_commit?.substring(0, 8)}...
          </span>
          <span className="badge badge-warning" style={{ marginLeft: '8px' }}>
            目标分支: {mergeResult.target_commit?.substring(0, 8)}...
          </span>
          <span className="badge badge-info" style={{ marginLeft: '8px' }}>
            基准: {mergeResult.base_commit?.substring(0, 8)}...
          </span>
        </div>
        <div style={{ display: 'flex', gap: '8px' }}>
          <button className="btn btn-secondary" onClick={onCancel}>
            取消
          </button>
          <button
            className="btn btn-primary"
            onClick={handleSubmit}
            disabled={Object.keys(resolutions).length < conflicts.length}
          >
            完成合并
          </button>
        </div>
      </div>
    </div>
  );
}

export default ThreeWayMergeView;
