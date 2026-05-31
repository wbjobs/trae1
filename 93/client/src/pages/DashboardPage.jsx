import React, { useState, useEffect, useCallback } from 'react'
import { useAuth } from '../hooks/AuthContext'
import { deviceApi } from '../api'
import DeviceList from '../components/DeviceList'
import DeviceStats from '../components/DeviceStats'
import ConfirmModal from '../components/ConfirmModal'

export default function DashboardPage() {
  const { user, logout } = useAuth()
  const [devices, setDevices] = useState([])
  const [loading, setLoading] = useState(true)
  const [error, setError] = useState('')
  const [modalDevice, setModalDevice] = useState(null)

  const loadDevices = useCallback(async () => {
    try {
      setLoading(true)
      const data = await deviceApi.getList()
      setDevices(data.credentials || [])
    } catch (err) {
      setError('加载设备列表失败')
    } finally {
      setLoading(false)
    }
  }, [])

  useEffect(() => {
    loadDevices()
  }, [loadDevices])

  const handleDelete = async (deviceId) => {
    try {
      await deviceApi.remove(deviceId)
      setDevices(devices.filter(d => d.id !== deviceId))
      setModalDevice(null)
    } catch (err) {
      setError('解绑设备失败')
    }
  }

  const formatDate = (dateString) => {
    if (!dateString) return '未知'
    return new Date(dateString).toLocaleString('zh-CN')
  }

  return (
    <>
      <div className="header">
        <div className="header-title">🔐 WebAuthn 认证中心</div>
        <div style={{ display: 'flex', alignItems: 'center', gap: '16px' }}>
          <span className="header-user">你好，{user?.displayName || user?.username}</span>
          <button className="btn btn-ghost" onClick={logout}>退出登录</button>
        </div>
      </div>

      <div className="main-content">
        <DeviceStats
          total={devices.length}
          maxDevices={5}
        />

        <div className="card">
          <div style={{ display: 'flex', justifyContent: 'space-between', alignItems: 'center', marginBottom: '20px' }}>
            <h2 style={{ fontSize: '18px', color: '#1a202c' }}>📱 已绑定设备</h2>
            <span style={{ fontSize: '13px', color: '#718096' }}>
              {devices.length} / 5 台设备
            </span>
          </div>

          {error && (
            <div className="alert alert-error">{error}</div>
          )}

          {loading ? (
            <div style={{ display: 'flex', justifyContent: 'center', padding: '40px' }}>
              <div className="spinner" style={{ borderTopColor: '#667eea' }}></div>
            </div>
          ) : (
            <DeviceList
              devices={devices}
              onDelete={(device) => setModalDevice(device)}
              formatDate={formatDate}
            />
          )}
        </div>
      </div>

      {modalDevice && (
        <ConfirmModal
          title="确认解绑设备"
          message={`确定要解绑「${modalDevice.name || '未命名设备'}」吗？解绑后需要重新注册才能使用该设备登录。`}
          onConfirm={() => handleDelete(modalDevice.id)}
          onCancel={() => setModalDevice(null)}
        />
      )}
    </>
  )
}
