import React from 'react'

export default function DeviceStats({ total, maxDevices }) {
  return (
    <div className="stats-grid">
      <div className="stat-card">
        <div className="stat-value">{total}</div>
        <div className="stat-label">已绑定设备</div>
      </div>
      <div className="stat-card">
        <div className="stat-value">{maxDevices}</div>
        <div className="stat-label">最大设备数</div>
      </div>
      <div className="stat-card">
        <div className="stat-value">{maxDevices - total}</div>
        <div className="stat-label">剩余绑定名额</div>
      </div>
    </div>
  )
}
