import React, { useState } from 'react';
import Dashboard from './components/Dashboard';
import EventLog from './components/EventLog';
import './index.css';

function App() {
    const [activeTab, setActiveTab] = useState('dashboard');

    return (
        <div className="app">
            <header className="app-header">
                <div className="header-content">
                    <div className="logo">
                        <div className="logo-icon">
                            <svg width="28" height="28" viewBox="0 0 24 24" fill="none" xmlns="http://www.w3.org/2000/svg">
                                <path d="M20 17.58V6.42c0-.67-.34-1.25-.84-1.59L12.69 1.44a1.83 1.83 0 00-1.97 0L4.63 4.83A1.83 1.83 0 003.7 6.42v11.16c0 .67.34 1.25.84 1.59l6.47 3.39c.52.28 1.15.44 1.8.44.65 0 1.28-.16 1.8-.44l6.47-3.39c.5-.34.92-.92.92-1.59zM2.29 17.13V6.87L11 2.33l8.71 4.54v10.26L11 21.67l-8.71-4.54z" fill="currentColor"/>
                                <path d="M12 17.25L4.71 13.5V7.25L12 3.5l7.29 3.75v6.25l-7.29 3.75z" fill="currentColor" opacity="0.5"/>
                            </svg>
                        </div>
                        <div>
                            <h1>NAS Audit</h1>
                            <span className="subtitle">企业文件操作审计系统</span>
                        </div>
                    </div>
                    <nav className="nav-tabs">
                        <button
                            className={`tab-btn ${activeTab === 'dashboard' ? 'active' : ''}`}
                            onClick={() => setActiveTab('dashboard')}
                        >
                            <span className="tab-icon">📊</span> 仪表盘
                        </button>
                        <button
                            className={`tab-btn ${activeTab === 'events' ? 'active' : ''}`}
                            onClick={() => setActiveTab('events')}
                        >
                            <span className="tab-icon">📋</span> 操作日志
                        </button>
                    </nav>
                    <div className="header-right">
                        <div className="status-indicator" id="statusIndicator">
                            <span className="status-dot"></span>
                            <span className="status-text">加载中...</span>
                        </div>
                    </div>
                </div>
            </header>
            <main className="app-main">
                {activeTab === 'dashboard' && <Dashboard />}
                {activeTab === 'events' && <EventLog />}
            </main>
        </div>
    );
}

export default App;
