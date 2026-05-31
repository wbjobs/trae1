import React from 'react';

export default function LogConsole({ logs }) {
  return (
    <div className="log-box">
      {logs.map((l, i) => (
        <div key={i} className={`log-entry ${l.level}`}>
          [{new Date(l.t).toLocaleTimeString()}] {l.msg}
        </div>
      ))}
    </div>
  );
}
