import React, { useState } from 'react'
import { Link, useNavigate } from 'react-router-dom'
import { authApi } from '../api'
import { createCredential, isWebAuthnSupported } from '../api/webauthn'
import { useAuth } from '../hooks/AuthContext'

export default function RegisterPage() {
  const [formData, setFormData] = useState({
    username: '',
    displayName: '',
    deviceName: '',
  })
  const [loading, setLoading] = useState(false)
  const [error, setError] = useState('')
  const navigate = useNavigate()
  const { login } = useAuth()

  const handleChange = (e) => {
    setFormData({ ...formData, [e.target.name]: e.target.value })
  }

  const handleSubmit = async (e) => {
    e.preventDefault()
    const { username, displayName, deviceName } = formData

    if (!username.trim()) {
      setError('请输入用户名')
      return
    }
    if (!displayName.trim()) {
      setError('请输入显示名称')
      return
    }
    if (!deviceName.trim()) {
      setError('请输入设备名称')
      return
    }
    if (!isWebAuthnSupported()) {
      setError('您的浏览器不支持 WebAuthn，请使用现代浏览器')
      return
    }

    setLoading(true)
    setError('')

    try {
      const options = await authApi.beginRegister({
        username,
        displayName,
        deviceName,
      })

      const challenge = options.response.challenge
      const credential = await createCredential(options)

      const result = await authApi.finishRegister(challenge, deviceName, credential)
      login(result.token, result.user)
      navigate('/')
    } catch (err) {
      if (err.name === 'NotAllowedError') {
        setError('注册被取消或超时')
      } else if (err.message) {
        setError(err.message)
      } else {
        setError('注册失败，请重试')
      }
    } finally {
      setLoading(false)
    }
  }

  return (
    <div className="container" style={{ paddingTop: '40px' }}>
      <div className="card">
        <div className="card-header">
          <h1>✨ 创建账户</h1>
          <p>使用生物识别或安全密钥注册</p>
        </div>

        {error && (
          <div className="alert alert-error">{error}</div>
        )}

        <form onSubmit={handleSubmit}>
          <div className="form-group">
            <label>用户名</label>
            <input
              type="text"
              name="username"
              value={formData.username}
              onChange={handleChange}
              placeholder="用于登录的用户名"
              disabled={loading}
              autoFocus
            />
          </div>

          <div className="form-group">
            <label>显示名称</label>
            <input
              type="text"
              name="displayName"
              value={formData.displayName}
              onChange={handleChange}
              placeholder="您的昵称或真实姓名"
              disabled={loading}
            />
          </div>

          <div className="form-group">
            <label>设备名称</label>
            <input
              type="text"
              name="deviceName"
              value={formData.deviceName}
              onChange={handleChange}
              placeholder="如：我的手机、工作电脑"
              disabled={loading}
            />
          </div>

          <button type="submit" className="btn btn-primary" disabled={loading}>
            {loading ? (
              <>
                <div className="spinner"></div>
                等待认证...
              </>
            ) : (
              <>注册并绑定设备</>
            )}
          </button>
        </form>

        <div style={{ textAlign: 'center', marginTop: '20px' }}>
          <p style={{ color: '#718096', fontSize: '14px' }}>
            已有账户？{' '}
            <Link to="/login" style={{ color: '#667eea', textDecoration: 'none', fontWeight: '500' }}>
              立即登录
            </Link>
          </p>
        </div>

        <div style={{ marginTop: '20px', padding: '12px', background: '#f7fafc', borderRadius: '8px' }}>
          <p style={{ fontSize: '12px', color: '#718096', textAlign: 'center' }}>
            🔒 您的私钥仅存储在设备上，永远不会发送到服务器
          </p>
        </div>
      </div>
    </div>
  )
}
