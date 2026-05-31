import React, { useState, useEffect } from 'react'

export default function AdditionalVerification({ methods, onComplete, onCancel }) {
  const [selectedMethod, setSelectedMethod] = useState(null)
  const [code, setCode] = useState('')
  const [loading, setLoading] = useState(false)
  const [error, setError] = useState('')
  const [countdown, setCountdown] = useState(0)
  const [sessionId, setSessionId] = useState(null)

  useEffect(() => {
    if (countdown > 0) {
      const timer = setTimeout(() => setCountdown(countdown - 1), 1000)
      return () => clearTimeout(timer)
    }
  }, [countdown])

  const methodLabels = {
    email_otp: { name: '邮箱验证码', icon: '📧', description: '发送验证码到您的邮箱' },
    sms_otp: { name: '短信验证码', icon: '📱', description: '发送验证码到您的手机' },
    security_questions: { name: '安全问题', icon: '🔐', description: '回答预设的安全问题' },
    webauthn_gesture: { name: '辅助手势', icon: '✋', description: '使用安全密钥手势验证' },
    device_biometric: { name: '设备生物识别', icon: '👆', description: '使用设备生物识别' },
  }

  const handleSendCode = async () => {
    if (!selectedMethod) return
    setLoading(true)
    setError('')

    try {
      const type = selectedMethod.includes('email') ? 'email' :
                   selectedMethod.includes('sms') ? 'sms' : 'email'

      const username = localStorage.getItem('username') || ''
      const response = await fetch('/api/recovery/send-code', {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify({ username, type }),
      })

      if (response.ok) {
        const data = await response.json()
        setSessionId(data.sessionId)
        setCountdown(60)
        setError('')
      } else {
        const errorText = await response.text()
        setError(errorText || '发送验证码失败')
      }
    } catch (err) {
      setError('网络错误，请重试')
    } finally {
      setLoading(false)
    }
  }

  const handleVerifyCode = async () => {
    if (!code.trim()) {
      setError('请输入验证码')
      return
    }

    setLoading(true)
    setError('')

    try {
      const response = await fetch('/api/recovery/verify-code', {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify({ sessionId, code }),
      })

      if (response.ok) {
        const data = await response.json()
        localStorage.setItem('recoveryToken', data.recoveryToken)
        onComplete(data.recoveryToken)
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

  if (!selectedMethod) {
    return (
      <div className="additional-verification" style={{
        marginTop: '16px',
        padding: '20px',
        background: 'white',
        borderRadius: '12px',
        boxShadow: '0 4px 12px rgba(0,0,0,0.1)',
      }}>
        <h3 style={{ fontSize: '16px', fontWeight: '600', marginBottom: '16px', color: '#1a202c' }}>
          🔒 需要额外验证
        </h3>
        <p style={{ fontSize: '13px', color: '#718096', marginBottom: '16px' }}>
          检测到本次登录风险较高，请选择以下方式之一完成验证：
        </p>

        <div style={{ display: 'flex', flexDirection: 'column', gap: '10px', marginBottom: '16px' }}>
          {(methods || ['email_otp']).map((method) => {
            const info = methodLabels[method] || { name: method, icon: '🔑', description: '使用此方式验证' }
            return (
              <button
                key={method}
                onClick={() => setSelectedMethod(method)}
                style={{
                  padding: '14px 16px',
                  border: '2px solid #e2e8f0',
                  borderRadius: '10px',
                  background: 'white',
                  cursor: 'pointer',
                  textAlign: 'left',
                  display: 'flex',
                  alignItems: 'center',
                  gap: '12px',
                  transition: 'all 0.2s',
                }}
                onMouseEnter={(e) => {
                  e.target.style.borderColor = '#667eea'
                  e.target.style.background = '#f7fafc'
                }}
                onMouseLeave={(e) => {
                  e.target.style.borderColor = '#e2e8f0'
                  e.target.style.background = 'white'
                }}
              >
                <span style={{ fontSize: '24px' }}>{info.icon}</span>
                <div>
                  <div style={{ fontWeight: '600', color: '#1a202c' }}>{info.name}</div>
                  <div style={{ fontSize: '12px', color: '#718096' }}>{info.description}</div>
                </div>
              </button>
            )
          })}
        </div>

        <button
          onClick={onCancel}
          style={{
            width: '100%',
            padding: '12px',
            border: 'none',
            borderRadius: '8px',
            background: '#edf2f7',
            color: '#4a5568',
            cursor: 'pointer',
            fontSize: '14px',
          }}
        >
          取消
        </button>
      </div>
    )
  }

  return (
    <div className="additional-verification" style={{
      marginTop: '16px',
      padding: '20px',
      background: 'white',
      borderRadius: '12px',
      boxShadow: '0 4px 12px rgba(0,0,0,0.1)',
    }}>
      <h3 style={{ fontSize: '16px', fontWeight: '600', marginBottom: '16px', color: '#1a202c' }}>
        {methodLabels[selectedMethod]?.icon || '🔑'} 输入验证码
      </h3>

      {error && (
        <div style={{
          padding: '10px 12px',
          background: '#fed7d7',
          color: '#c53030',
          borderRadius: '8px',
          fontSize: '13px',
          marginBottom: '12px',
        }}>
          {error}
        </div>
      )}

      {!sessionId ? (
        <div>
          <p style={{ fontSize: '13px', color: '#718096', marginBottom: '16px' }}>
            点击下方按钮发送验证码
          </p>
          <button
            onClick={handleSendCode}
            disabled={loading || countdown > 0}
            style={{
              width: '100%',
              padding: '12px 24px',
              border: 'none',
              borderRadius: '8px',
              background: countdown > 0 ? '#edf2f7' : 'linear-gradient(135deg, #667eea 0%, #764ba2 100%)',
              color: countdown > 0 ? '#718096' : 'white',
              cursor: loading || countdown > 0 ? 'not-allowed' : 'pointer',
              fontSize: '14px',
              fontWeight: '600',
              display: 'flex',
              alignItems: 'center',
              justifyContent: 'center',
              gap: '8px',
            }}
          >
            {loading ? (
              <>
                <div style={{
                  width: '16px', height: '16px',
                  border: '2px solid rgba(255,255,255,0.3)',
                  borderTopColor: 'white',
                  borderRadius: '50%',
                  animation: 'spin 0.8s linear infinite',
                }} />
                发送中...
              </>
            ) : countdown > 0 ? (
              `重新发送 (${countdown}s)`
            ) : (
              '发送验证码'
            )}
          </button>
        </div>
      ) : (
        <div>
          <p style={{ fontSize: '13px', color: '#718096', marginBottom: '12px' }}>
            验证码已发送，请输入6位验证码：
          </p>
          <input
            type="text"
            value={code}
            onChange={(e) => setCode(e.target.value)}
            placeholder="请输入验证码"
            maxLength={6}
            style={{
              width: '100%',
              padding: '12px 16px',
              border: '2px solid #e2e8f0',
              borderRadius: '8px',
              fontSize: '16px',
              textAlign: 'center',
              letterSpacing: '4px',
              marginBottom: '12px',
            }}
            disabled={loading}
          />
          <div style={{ display: 'flex', gap: '10px' }}>
            <button
              onClick={() => setSelectedMethod(null)}
              style={{
                flex: 1,
                padding: '12px',
                border: 'none',
                borderRadius: '8px',
                background: '#edf2f7',
                color: '#4a5568',
                cursor: 'pointer',
                fontSize: '14px',
              }}
            >
              返回
            </button>
            <button
              onClick={handleVerifyCode}
              disabled={loading || code.length !== 6}
              style={{
                flex: 2,
                padding: '12px 24px',
                border: 'none',
                borderRadius: '8px',
                background: code.length === 6 ? 'linear-gradient(135deg, #667eea 0%, #764ba2 100%)' : '#edf2f7',
                color: code.length === 6 ? 'white' : '#718096',
                cursor: loading || code.length !== 6 ? 'not-allowed' : 'pointer',
                fontSize: '14px',
                fontWeight: '600',
              }}
            >
              {loading ? '验证中...' : '验证'}
            </button>
          </div>
        </div>
      )}
    </div>
  )
}
