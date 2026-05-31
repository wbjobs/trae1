import { useEffect, useState } from 'react';
import { useParams } from 'react-router-dom';
import { docsApi } from '../services/api';
import type { DocPayload } from '../types';

export default function DocsPage() {
  const { componentName } = useParams<{ componentName: string }>();
  const [doc, setDoc] = useState<DocPayload | null>(null);
  const [error, setError] = useState('');

  useEffect(() => {
    if (!componentName) return;
    setError('');
    docsApi
      .get(componentName)
      .then(setDoc)
      .catch((err: unknown) => setError(err instanceof Error ? err.message : String(err)));
  }, [componentName]);

  if (error) {
    return (
      <div>
        <div className="page-header"><h2>组件文档</h2></div>
        <div className="card"><p className="conflict">{error}</p></div>
      </div>
    );
  }

  if (!doc) {
    return (
      <div>
        <div className="page-header"><h2>组件文档</h2></div>
        <div className="card"><p className="empty">加载中...</p></div>
      </div>
    );
  }

  return (
    <div>
      <div className="page-header">
        <h2>
          {doc.componentName} <span className="tag stable">{doc.latestVersion}</span>
        </h2>
        <a
          className="btn secondary"
          href={docsApi.markdown(doc.componentName)}
          download={`${doc.componentName}.md`}
        >
          下载 Markdown
        </a>
      </div>

      {doc.description && (
        <div className="card">
          <h3>描述</h3>
          <p>{doc.description}</p>
        </div>
      )}

      <div className="grid grid-2">
        <div className="card">
          <h3>安装</h3>
          <pre className="code-block">{doc.installation.npm}</pre>
          <pre className="code-block">{doc.installation.yarn}</pre>
          <pre className="code-block">{doc.installation.pnpm}</pre>
        </div>

        <div className="card">
          <h3>引入方式</h3>
          <div className="form-row">
            <label>ESM</label>
            <pre className="code-block">{doc.importGuide.esm}</pre>
          </div>
          <div className="form-row">
            <label>CJS</label>
            <pre className="code-block">{doc.importGuide.cjs}</pre>
          </div>
          {doc.importGuide.treeShake.length > 0 && (
            <div className="form-row">
              <label>按需引入 (Tree Shaking)</label>
              {doc.importGuide.treeShake.map((p, i) => (
                <pre key={i} className="code-block">{p}</pre>
              ))}
            </div>
          )}
        </div>
      </div>

      <div className="card">
        <h3>版本历史</h3>
        <table>
          <thead>
            <tr><th>版本</th><th>标签</th><th>更新日志</th><th>日期</th></tr>
          </thead>
          <tbody>
            {doc.versions.map((v, i) => (
              <tr key={i}>
                <td>{v.version}</td>
                <td><span className={`tag ${v.tag}`}>{v.tag}</span></td>
                <td>{v.changelog ?? '-'}</td>
                <td>{new Date(v.createdAt).toLocaleDateString()}</td>
              </tr>
            ))}
          </tbody>
        </table>
      </div>

      {doc.dependencies.length > 0 && (
        <div className="card">
          <h3>依赖</h3>
          <table>
            <thead>
              <tr><th>名称</th><th>版本</th></tr>
            </thead>
            <tbody>
              {doc.dependencies.map((d, i) => (
                <tr key={i}>
                  <td>{d.name}</td>
                  <td>{d.version}</td>
                </tr>
              ))}
            </tbody>
          </table>
        </div>
      )}

      {doc.readme && (
        <div className="card">
          <h3>README</h3>
          <pre className="code-block">{doc.readme}</pre>
        </div>
      )}
    </div>
  );
}
