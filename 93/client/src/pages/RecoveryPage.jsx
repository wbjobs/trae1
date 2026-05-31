import React, { useState } from 'react'
import { Link, useNavigate } from 'react-router-dom'
import { authApi } from '../api'
import { createCredential, isWebAuthnSupported } from '../api/webauthn'
import { useAuth } from '../hooks/AuthContext'

export default function RecoveryPage() {
  const [step, setStep] = useState(1)
  const [username, setUsername] = useState('')
  const [recoveryCode, setRecoveryCode] = useState('')
  const [backupPassword, setBackupPassword] = useState('')
  const [deviceName, setDeviceName] = useState('')
  const [loading, setLoading] = useState(false)
  const [error, setError] = useState('')
  const [recoveryToken, setRecoveryToken] = useState(null)
  const [recoveryTokenId, setRecoveryTokenId] = useState(null)
  const navigate = useNavigate()
  const { login } = useAuth()

  const handleVerifyRecoveryCode = async (e) => {
    e.preventDefault()
    if (!username.trim() || !recoveryCode.trim()) {
      setError('请输入用户名和恢复码')
      return
    }

    setLoading(true)
    setError('')

    try {
      const response = await fetch('/api/recovery/verify-recovery-code', {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify({ username, recoveryCode }),
      })

      if (response.ok) {
        const data = await response.json()
        setRecoveryToken(data.recoveryToken)
        setRecoveryTokenId(data.recoveryTokenId)
        setStep(2)
      } else {
        const errorText = await response.text()
        setError(errorText || '验证失败')
      }
    } catch (err) {
      setError('网络错误，请重试')
    } finally {
      setLoading(false)
    }
  }

  const handleBeginRecovery = async (e) => {
    e.preventDefault()
    if (!backupPassword.trim() || !deviceName.trim()) {
      setError('请输入备份密码和设备名称')
      return
    }
    if (!isWebAuthnSupported()) {
      setError('您的浏览器不支持 WebAuthn')
      return
    }

    setLoading(true)
    setError('')

    try {
      const response = await fetch('/api/recovery/begin', {
        method: 'POST',
        headers: {
          'Content-Type': 'application/json',
          'Authorization': `Bearer ${recoveryToken}`,
        },
        body: JSON.stringify({
          recoveryTokenId,
          password: backupPassword,
          deviceName,
        }),
      })

      if (response.ok) {
        const data = await response.json()
        const options = data.options
        const credential = await createCredential(options)

        const finishResponse = await fetch(`/api/recovery/finish?challenge=${data.sessionId}&recoveryTokenId=${recoveryTokenId}&deviceName=${encodeURIComponent(deviceName)}`, {
          method: 'POST',
          headers: {
            'Content-Type': 'application/json',
            'Authorization': `Bearer ${recoveryToken}`,
          },
          body: JSON.stringify(credential),
        })

        if (finishResponse.ok) {
          const result = await finishResponse.json()
          login(result.token, result.user)
          navigate('/')
        } else {
          const errorText = await finishResponse.text()
          setError(errorText || '设备绑定失败')
        }
      } else {
        const errorText = await response.text()
        setError(errorText || '开始恢复失败')
      }
    } catch (err) {
      if (err.name === 'NotAllowedError') {
        setError('认证被取消或超时')
      } else {
        setError('恢复过程出错，请重试')
      }
    } finally {
      setLoading(false)
    }
  }

  return (
    <div className="container" style={{ paddingTop: '40px' }}>
      <div className="card">
        <div className="card-header">
          <h1>🔄 账户恢复</h1>
          <p>使用恢复码重新绑定设备</p>
        </div>

        {error && (
          <div className="alert alert-error">{error}</div>
        )}

        {step === 1 && (
          <form onSubmit={handleVerifyRecoveryCode}>
            <div className="form-group">
              <label>用户名</label>
              <input
                type="text"
                value={username}
                onChange={(e) => setUsername(e.target.value)}
                placeholder="请输入用户名"
                disabled={loading}
                autoFocus
              />
            </div>

            <div className="form-group">
              <label>恢复码</label>
              <input
                type="text"
                value={recoveryCode}
                onChange={(e) => setRecoveryCode(e.target.value.toUpperCase())}
                placeholder="XXXX-XXXX-XXXX-XXXX"
                disabled={loading}
                style={{ textTransform: 'uppercase', letterSpacing: '2px' }}
              />
              <p style={{ fontSize: '12px', color: '#718096', marginTop: '4px' }}>
                恢复码在创建备份时生成
              </p>
            </div>

            <button type="submit" className="btn btn-primary" disabled={loading}>
              {loading ? (
                <>
                  <div className="spinner"></div>
                  验证中...
                </>
              ) : (
                '验证恢复码'
              )}
            </button>
          </form>
        )}

        {step === 2 && (
          <form onSubmit={handleBeginRecovery}>
            <div style={{
              padding: '12px',
              background: '#c6f6d5',
              color: '#22543d',
              borderRadius: '8px',
              marginBottom: '16px',
              fontSize: '13px',
            }}>
              ✓ 恢复码验证成功！现在请输入备份密码来解密您的凭证。
            </div>

            <div className="form-group">
              <label>备份密码</label>
              <input
                type="password"
                value={backupPassword}
                onChange={(e) => setBackupPassword(e.target.value)}
                placeholder="创建备份时设置的密码"
                disabled={loading}
                autoFocus
              />
            </div>

            <div className="form-group">
              <label>新设备名称</label>
              <input
                type="text"
                value={deviceName}
                onChange={(e) => setDeviceName(e.target.value)}
                placeholder="为当前设备命名"
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
                '绑定新设备'
              )}
            </button>
          </form>
        )}

        <div style={{ textAlign: 'center', marginTop: '20px' }}>
          <p style={{ color: '#718096', fontSize: '14px' }}>
            记起密码了？{' '}
            <Link to="/login" style={{ color: '#667eea', textDecoration: 'none', fontWeight: '500' }}>
              返回登录
            </Link>
          </p>
        </div>

        <div style={{ marginTop: '20px', padding: '12px', background: '#f7fafc', borderRadius: '8px' }}>
          <p style={{ fontSize: '12px', color: '#718096', textAlign: 'center' }}>
            💡 没有恢复码？请联系管理员或使用其他已绑定设备登录后创建备份
          </p>
        </div>
      </div>
    </div>
  )
}
