import React, { useState } from 'react';
import MasterView from './components/MasterView.jsx';
import WorkerView from './components/WorkerView.jsx';
import { randomId } from './utils/utils.js';

export default function App() {
  const [role, setRole] = useState(null);
  const [nodeId] = useState(() => randomId());
  const [name, setName] = useState('');
  const [serverUrl, setServerUrl] = useState(
    () => (typeof window !== 'undefined'
      ? `ws://${window.location.hostname}:4000`
      : 'ws://localhost:4000')
  );

  if (!role) {
    return (
      <div className="app">
        <header className="app-header">
          <h1>Distributed Compute Mesh</h1>
        </header>
        <main style={{ display: 'grid', placeItems: 'center', height: '80vh' }}>
          <div className="panel" style={{ minWidth: 360 }}>
            <h2>Join Network</h2>
            <div className="task-form">
              <div className="row">
                <label>Signaling Server</label>
                <input
                  type="text"
                  style={{ flex: 1 }}
                  value={serverUrl}
                  onChange={(e) => setServerUrl(e.target.value)}
                  placeholder="ws://localhost:4000"
                />
              </div>
              <div className="row">
                <label>Your Name</label>
                <input
                  type="text"
                  style={{ flex: 1 }}
                  value={name}
                  onChange={(e) => setName(e.target.value)}
                  placeholder="optional"
                />
              </div>
              <div className="row" style={{ justifyContent: 'space-around', marginTop: 10 }}>
                <button onClick={() => setRole('master')}>I am Master</button>
                <button className="secondary" onClick={() => setRole('worker')}>I am Worker</button>
              </div>
              <div style={{ fontSize: 12, color: '#8a93c8', marginTop: 8, textAlign: 'center' }}>
                Node ID: {nodeId}
              </div>
            </div>
          </div>
        </main>
        <div className="footer-note">
          Master distributes tasks; Workers compute chunks. Open multiple browser tabs to form a mesh.
        </div>
      </div>
    );
  }

  return (
    <div className="app">
      <header className="app-header">
        <h1>
          Distributed Compute Mesh · <span className={`badge ${role}`}>{role.toUpperCase()}</span>
        </h1>
        <div className="role-selector">
          <span style={{ fontSize: 12, color: '#a5b4ff' }}>{name || nodeId}</span>
          <button className="secondary" onClick={() => setRole(null)}>Change Role</button>
        </div>
      </header>
      {role === 'master' ? (
        <MasterView serverUrl={serverUrl} nodeId={nodeId} name={name || 'Master'} />
      ) : (
        <WorkerView serverUrl={serverUrl} nodeId={nodeId} name={name || 'Worker'} />
      )}
      <div className="footer-note">
        Open multiple browser tabs — each as a Worker — to scale the mesh.
      </div>
    </div>
  );
}
