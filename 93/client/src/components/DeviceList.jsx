import React from 'react'

export default function DeviceList({ devices, onDelete, formatDate }) {
  if (devices.length === 0) {
    return (
      <div className="empty-state">
        <div className="empty-state-icon">📱</div>
        <p>暂无绑定设备</p>
        <p style={{ fontSize: '13px', marginTop: '8px' }}>
          使用其他设备登录时可以绑定新设备
        </p>
      </div>
    )
  }

  return (
    <div>
      {devices.map((device) => (
        <div key={device.id} className="device-card">
          <div className="device-card-header">
            <div>
              <div className="device-name">{device.name || '未命名设备'}</div>
              <div className="device-info" style={{ marginTop: '4px' }}>
                <strong>认证类型:</strong> {device.attestationType || '未知'}
              </div>
            </div>
            <button
              className="btn btn-danger"
              onClick={() => onDelete(device)}
            >
              解绑
            </button>
          </div>
          <div className="device-info">
            <div>
              <strong>绑定时间:</strong> {formatDate(device.createdAt)}
            </div>
            <div>
              <strong>最后使用:</strong> {formatDate(device.lastUsedAt)}
            </div>
            {device.backupEligible && (
              <div style={{ marginTop: '8px' }}>
                <span style={{
                  background: '#e6fffa',
                  color: '#0891b2',
                  padding: '4px 8px',
                  borderRadius: '4px',
                  fontSize: '12px',
                }}>
                  💾 支持可恢复凭据
                </span>
              </div>
            )}
          </div>
        </div>
      ))}
    </div>
  )
}
