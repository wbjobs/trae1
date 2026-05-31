import React, { useState } from 'react';
import { Routes, Route, Link, useLocation } from 'react-router-dom';
import UploadPage from './pages/UploadPage';
import HistoryPage from './pages/HistoryPage';
import DiffPage from './pages/DiffPage';
import BranchPage from './pages/BranchPage';
import TagPage from './pages/TagPage';
import MergeResolvePage from './pages/MergeResolvePage';
import './App.css';

function App() {
  const [activeTab, setActiveTab] = useState('upload');
  const location = useLocation();

  const isMergeResolvePage = location.pathname.startsWith('/merge/resolve');

  return (
    <div className="app">
      {!isMergeResolvePage && (
        <header className="app-header">
          <div className="logo">
            <h1>ConfigVCS</h1>
            <span className="subtitle">配置文件版本管理系统</span>
          </div>
          <nav className="nav-tabs">
            <Link
              to="/"
              className={`nav-tab ${activeTab === 'upload' ? 'active' : ''}`}
              onClick={() => setActiveTab('upload')}
            >
              上传配置
            </Link>
            <Link
              to="/history"
              className={`nav-tab ${activeTab === 'history' ? 'active' : ''}`}
              onClick={() => setActiveTab('history')}
            >
              版本历史
            </Link>
            <Link
              to="/diff"
              className={`nav-tab ${activeTab === 'diff' ? 'active' : ''}`}
              onClick={() => setActiveTab('diff')}
            >
              差异对比
            </Link>
            <Link
              to="/branches"
              className={`nav-tab ${activeTab === 'branches' ? 'active' : ''}`}
              onClick={() => setActiveTab('branches')}
            >
              分支管理
            </Link>
            <Link
              to="/tags"
              className={`nav-tab ${activeTab === 'tags' ? 'active' : ''}`}
              onClick={() => setActiveTab('tags')}
            >
              标签管理
            </Link>
          </nav>
        </header>
      )}

      <main className={`app-main ${isMergeResolvePage ? 'full-width' : ''}`}>
        <Routes>
          <Route path="/" element={<UploadPage />} />
          <Route path="/history" element={<HistoryPage />} />
          <Route path="/diff" element={<DiffPage />} />
          <Route path="/branches" element={<BranchPage />} />
          <Route path="/tags" element={<TagPage />} />
          <Route path="/merge/resolve" element={<MergeResolvePage />} />
        </Routes>
      </main>
    </div>
  );
}

export default App;
