import React, { useState } from 'react'
import { Link, useNavigate } from 'react-router-dom'
import { authApi } from '../api'
import { getCredential, isWebAuthnSupported } from '../api/webauthn'
import { useAuth } from '../hooks/AuthContext'
import RiskIndicator from '../components/RiskIndicator'
import AdditionalVerification from '../components/AdditionalVerification'

export default function LoginPage() {
  const [username, setUsername] = useState('')
  const [loading, setLoading] = useState(false)
  const [error, setError] = useState('')
  const [risk, setRisk] = useState(null)
  const [showAdditionalAuth, setShowAdditionalAuth] = useState(false)
  const navigate = useNavigate()
  const { login } = useAuth()

  const handleSubmit = async (e) => {
    e.preventDefault()
    if (!username.trim()) {
      setError('请输入用户名')
      return
    }
    if (!isWebAuthnSupported()) {
      setError('您的浏览器不支持 WebAuthn，请使用现代浏览器')
      return
    }

    setLoading(true)
    setError('')
    setRisk(null)

    try {
      localStorage.setItem('username', username)

      const response = await authApi.beginLogin({ username })

      if (response.risk) {
        setRisk(response.risk)

        if (response.requiresAdditionalAuth) {
          setShowAdditionalAuth(true)
          setLoading(false)
          return
        }
      }

      const options = response.options || response
      const challenge = options.response?.challenge || response.challenge
      const credential = await getCredential(options)

      const result = await authApi.finishLogin(challenge, credential)
      login(result.token, result.user)
      navigate('/')
    } catch (err) {
      if (err.name === 'NotAllowedError') {
        setError('认证被取消或超时')
      } else if (err.message) {
        setError(err.message)
      } else {
        setError('登录失败，请重试')
      }
    } finally {
      setLoading(false)
    }
  }

  const handleAdditionalAuthComplete = async (recoveryToken) => {
    setShowAdditionalAuth(false)
    setLoading(true)

    try {
      const options = await authApi.beginLogin({ username })
      const challenge = options.response?.challenge || options.challenge
      const credential = await getCredential(options)

      const result = await authApi.finishLogin(challenge, credential)
      login(result.token, result.user)
      navigate('/')
    } catch (err) {
      if (err.name === 'NotAllowedError') {
        setError('认证被取消或超时')
      } else if (err.message) {
        setError(err.message)
      } else {
        setError('登录失败，请重试')
      }
    } finally {
      setLoading(false)
    }
  }

  return (
    <div className="container" style={{ paddingTop: '60px' }}>
      <div className="card">
        <div className="card-header">
          <h1>🔐 欢迎回来</h1>
          <p>使用通行密钥安全登录</p>
        </div>

        {error && (
          <div className="alert alert-error">{error}</div>
        )}

        {risk && <RiskIndicator risk={risk} />}

        {showAdditionalAuth && risk?.action?.methods ? (
          <AdditionalVerification
            methods={risk.action.methods}
            onComplete={handleAdditionalAuthComplete}
            onCancel={() => {
              setShowAdditionalAuth(false)
              setRisk(null)
            }}
          />
        ) : (
          <form onSubmit={handleSubmit}>
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

            <button type="submit" className="btn btn-primary" disabled={loading}>
              {loading ? (
                <>
                  <div className="spinner"></div>
                  等待认证...
                </>
              ) : (
                <>使用通行密钥登录</>
              )}
            </button>
          </form>
        )}

        {!showAdditionalAuth && (
          <div style={{ textAlign: 'center', marginTop: '20px' }}>
            <p style={{ color: '#718096', fontSize: '14px' }}>
              还没有账户？{' '}
              <Link to="/register" style={{ color: '#667eea', textDecoration: 'none', fontWeight: '500' }}>
                立即注册
              </Link>
            </p>
            <p style={{ color: '#718096', fontSize: '14px', marginTop: '8px' }}>
              丢失设备？{' '}
              <Link to="/recovery" style={{ color: '#667eea', textDecoration: 'none', fontWeight: '500' }}>
                恢复账户
              </Link>
            </p>
          </div>
        )}

        <div style={{ marginTop: '20px', padding: '12px', background: '#f7fafc', borderRadius: '8px' }}>
          <p style={{ fontSize: '12px', color: '#718096', textAlign: 'center' }}>
            💡 支持指纹、面部识别或安全密钥（如 YubiKey）
          </p>
        </div>
      </div>
    </div>
  )
}
