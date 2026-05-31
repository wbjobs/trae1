import { useEffect, useState } from 'react';
import { useParams } from 'react-router-dom';
import { bundleApi } from '../services/api';
import type { BundleReport } from '../types';

export default function BundlePage() {
  const { versionId } = useParams<{ versionId: string }>();
  const [report, setReport] = useState<BundleReport | null>(null);
  const [error, setError] = useState('');
  const [loading, setLoading] = useState(false);

  useEffect(() => {
    if (!versionId) return;
    setLoading(true);
    setError('');
    bundleApi
      .analyze(versionId)
      .then(setReport)
      .catch((err: unknown) => setError(err instanceof Error ? err.message : String(err)))
      .finally(() => setLoading(false));
  }, [versionId]);

  if (error) {
    return (
      <div>
        <div className="page-header"><h2>打包体积分析</h2></div>
        <div className="card"><p className="conflict">{error}</p></div>
      </div>
    );
  }

  if (loading || !report) {
    return (
      <div>
        <div className="page-header"><h2>打包体积分析</h2></div>
        <div className="card"><p className="empty">加载中...</p></div>
      </div>
    );
  }

  const compressionPct = (1 - report.compressionRatio) * 100;

  return (
    <div>
      <div className="page-header">
        <h2>
          打包分析：{report.componentName} @ {report.version}
        </h2>
      </div>

      <div className="grid grid-2">
        <div className="card">
          <h3>体积概览</h3>
          <table>
            <tbody>
              <tr>
                <td>原始体积</td>
                <td><strong>{report.bundleSize.rawFormatted}</strong></td>
              </tr>
              <tr>
                <td>Gzip 压缩</td>
                <td>{report.bundleSize.gzipFormatted}</td>
              </tr>
              <tr>
                <td>Brotli 压缩</td>
                <td>{report.bundleSize.brotliFormatted}</td>
              </tr>
              <tr>
                <td>压缩率</td>
                <td>
                  {(compressionPct).toFixed(1)}%
                  <span style={{ marginLeft: 8, color: compressionPct > 50 ? '#059669' : '#dc2626' }}>
                    {compressionPct > 50 ? '✓ 良好' : '⚠ 偏高'}
                  </span>
                </td>
              </tr>
              <tr>
                <td>Tree Shaking 潜力</td>
                <td>{(report.treeShakingPotential * 100).toFixed(1)}%</td>
              </tr>
            </tbody>
          </table>
        </div>

        <div className="card">
          <h3>统计信息</h3>
          <table>
            <tbody>
              <tr><td>导出成员数</td><td>{report.exportCount}</td></tr>
              <tr><td>依赖数量</td><td>{report.dependencyCount}</td></tr>
              <tr><td>未使用导出</td><td>{report.unusedExports.length}</td></tr>
            </tbody>
          </table>
          {report.dependencyNames.length > 0 && (
            <div style={{ marginTop: 12 }}>
              <p style={{ fontSize: 13, color: '#6b7280' }}>依赖列表：</p>
              {report.dependencyNames.map((n, i) => (
                <span key={i} className="tag stable" style={{ marginRight: 4 }}>{n}</span>
              ))}
            </div>
          )}
        </div>
      </div>

      {report.perExportSize.length > 0 && (
        <div className="card">
          <h3>各导出成员预估体积</h3>
          <table>
            <thead>
              <tr><th>导出</th><th>预估体积</th></tr>
            </thead>
            <tbody>
              {report.perExportSize.map((item, i) => (
                <tr key={i}>
                  <td><code>{item.name}</code></td>
                  <td>{(item.estimatedBytes / 1024).toFixed(2)} KB</td>
                </tr>
              ))}
            </tbody>
          </table>
        </div>
      )}

      {report.unusedExports.length > 0 && (
        <div className="card">
          <h3>可能未使用的导出</h3>
          {report.unusedExports.map((e, i) => (
            <span key={i} className="tag deprecated" style={{ marginRight: 4 }}>{e}</span>
          ))}
          <p style={{ fontSize: 13, color: '#6b7280', marginTop: 8 }}>
            这些导出未在 README 或预览源码中被引用，建议确认是否需要保留
          </p>
        </div>
      )}

      <div className="card">
        <h3>优化建议</h3>
        <ul>
          {report.optimizationSuggestions.map((s, i) => (
            <li key={i} style={{ marginBottom: 4 }}>{s}</li>
          ))}
        </ul>
      </div>
    </div>
  );
}
