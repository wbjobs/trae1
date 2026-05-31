import { NavLink, Route, Routes, Navigate } from 'react-router-dom';
import ComponentsPage from './pages/ComponentsPage';
import VersionsPage from './pages/VersionsPage';
import DependenciesPage from './pages/DependenciesPage';
import PreviewPage from './pages/PreviewPage';
import DocsPage from './pages/DocsPage';
import BundlePage from './pages/BundlePage';

export default function App() {
  return (
    <div className="app-shell">
      <aside className="sidebar">
        <h1>组件库管理平台</h1>
        <NavLink to="/components">组件管理</NavLink>
        <NavLink to="/dependencies">依赖冲突检测</NavLink>
      </aside>
      <main className="main">
        <Routes>
          <Route path="/" element={<Navigate to="/components" replace />} />
          <Route path="/components" element={<ComponentsPage />} />
          <Route path="/versions/:componentId" element={<VersionsPage />} />
          <Route path="/preview/:versionId" element={<PreviewPage />} />
          <Route path="/bundle/:versionId" element={<BundlePage />} />
          <Route path="/dependencies" element={<DependenciesPage />} />
          <Route path="/docs/:componentName" element={<DocsPage />} />
          <Route path="*" element={<Navigate to="/components" replace />} />
        </Routes>
      </main>
    </div>
  );
}
