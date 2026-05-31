import { useEffect, useRef, useState } from 'react';
import { useParams } from 'react-router-dom';
import { previewApi } from '../services/api';
import type { PreviewPayload } from '../types';

export default function PreviewPage() {
  const { versionId } = useParams<{ versionId: string }>();
  const [data, setData] = useState<PreviewPayload | null>(null);
  const [error, setError] = useState<string>('');
  const [iframeError, setIframeError] = useState(false);
  const iframeRef = useRef<HTMLIFrameElement>(null);

  useEffect(() => {
    if (!versionId) return;
    setError('');
    setIframeError(false);
    previewApi
      .get(versionId)
      .then(setData)
      .catch((err: unknown) => {
        const msg = err instanceof Error ? err.message : String(err);
        setError(`预览数据加载失败: ${msg}`);
      });
  }, [versionId]);

  const buildIframeSrc = (): string => {
    if (!data?.previewSource) return '';
    const html = `
<!DOCTYPE html>
<html>
<head>
<meta charset="UTF-8">
<style>
  body { margin: 0; padding: 24px; font-family: -apple-system, BlinkMacSystemFont, sans-serif; }
  .component-wrap { padding: 16px; border: 1px solid #e5e7eb; border-radius: 8px; }
</style>
</head>
<body>
<div class="component-wrap">
${data.previewSource}
</div>
</body>
</html>`;
    return 'data:text/html;charset=utf-8,' + encodeURIComponent(html);
  };

  if (error) {
    return (
      <div>
        <div className="page-header">
          <h2>组件预览</h2>
        </div>
        <div className="card">
          <p className="conflict">{error}</p>
        </div>
      </div>
    );
  }

  if (!data) {
    return (
      <div>
        <div className="page-header">
          <h2>组件预览</h2>
        </div>
        <div className="card">
          <p className="empty">加载中...</p>
        </div>
      </div>
    );
  }

  return (
    <div>
      <div className="page-header">
        <h2>
          预览：{data.componentName} @ {data.version}
        </h2>
      </div>

      <div className="grid grid-2">
        <div className="card">
          <h3>在线预览</h3>
          {data.previewSource ? (
            iframeError ? (
              <div className="preview-sandbox">
                <p className="conflict">预览渲染失败，以下为原始源码：</p>
                <pre className="code-block">{data.previewSource}</pre>
              </div>
            ) : (
              <iframe
                ref={iframeRef}
                src={buildIframeSrc()}
                title={`${data.componentName} preview`}
                style={{ width: '100%', minHeight: 300, border: '1px solid #e5e7eb', borderRadius: 8 }}
                onError={() => setIframeError(true)}
                onLoad={() => setIframeError(false)}
              />
            )
          ) : (
            <div className="preview-sandbox">
              <p className="empty">该版本尚未提供 previewSource 预览源码</p>
            </div>
          )}
        </div>

        <div className="card">
          <h3>按需引入配置</h3>
          <div className="form-row">
            <label>ESM</label>
            <pre className="code-block">{data.importConfig.esm}</pre>
          </div>
          <div className="form-row">
            <label>CJS</label>
            <pre className="code-block">{data.importConfig.cjs}</pre>
          </div>
          <div className="form-row">
            <label>UNPKG CDN</label>
            <pre className="code-block">{data.importConfig.unpkg}</pre>
          </div>
          {data.importConfig.treeShakePaths.length > 0 && (
            <div className="form-row">
              <label>按需单独引入 (Tree Shaking)</label>
              {data.importConfig.treeShakePaths.map((p, i) => (
                <pre key={i} className="code-block">{p}</pre>
              ))}
            </div>
          )}
          {data.importConfig.sideEffects.length > 0 && (
            <div className="form-row">
              <label>副作用路径</label>
              {data.importConfig.sideEffects.map((p, i) => (
                <div key={i} className="code-block">{p}</div>
              ))}
            </div>
          )}
        </div>
      </div>

      {data.exports.length > 0 && (
        <div className="card">
          <h3>导出成员</h3>
          <div>
            {data.exports.map((e, i) => (
              <span key={i} className="tag stable">{e}</span>
            ))}
          </div>
        </div>
      )}
    </div>
  );
}
