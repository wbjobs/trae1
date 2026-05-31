import React from 'react'

export default function RiskIndicator({ risk }) {
  if (!risk) return null

  const { score, level, factors, action } = risk

  const getLevelConfig = (levelName) => {
    switch (levelName) {
      case 'low':
        return { color: '#48bb78', bg: '#c6f6d5', label: '低风险', icon: '✓' }
      case 'medium':
        return { color: '#d69e2e', bg: '#fefcbf', label: '中风险', icon: '⚠' }
      case 'high':
        return { color: '#dd6b20', bg: '#feebc8', label: '高风险', icon: '⚠' }
      case 'critical':
        return { color: '#e53e3e', bg: '#fed7d7', label: '极高风险', icon: '✕' }
      default:
        return { color: '#718096', bg: '#edf2f7', label: '未知', icon: '?' }
    }
  }

  const config = getLevelConfig(level)

  return (
    <div className="risk-indicator" style={{
      marginTop: '16px',
      padding: '16px',
      background: config.bg,
      borderRadius: '12px',
      border: `2px solid ${config.color}`,
    }}>
      <div style={{ display: 'flex', justifyContent: 'space-between', alignItems: 'center', marginBottom: '12px' }}>
        <div style={{ display: 'flex', alignItems: 'center', gap: '8px' }}>
          <span style={{ fontSize: '24px' }}>{config.icon}</span>
          <span style={{ fontWeight: '600', color: config.color }}>{config.label}</span>
        </div>
        <div style={{ textAlign: 'right' }}>
          <div style={{ fontSize: '12px', color: '#718096' }}>风险评分</div>
          <div style={{ fontSize: '20px', fontWeight: '700', color: config.color }}>
            {score.toFixed(0)}<span style={{ fontSize: '12px' }}>/100</span>
          </div>
        </div>
      </div>

      <div style={{
        height: '8px',
        background: '#e2e8f0',
        borderRadius: '4px',
        overflow: 'hidden',
        marginBottom: '12px',
      }}>
        <div style={{
          width: `${score}%`,
          height: '100%',
          background: `linear-gradient(90deg, #48bb78 0%, #d69e2e 50%, #e53e3e 100%)`,
          transition: 'width 0.3s ease',
        }} />
      </div>

      {action && action.type !== 'allow' && (
        <div style={{
          padding: '12px',
          background: 'rgba(255,255,255,0.6)',
          borderRadius: '8px',
          marginBottom: '12px',
        }}>
          <div style={{ fontSize: '13px', fontWeight: '600', color: '#4a5568', marginBottom: '4px' }}>
            需要额外验证
          </div>
          <div style={{ fontSize: '12px', color: '#718096' }}>
            {action.description}
          </div>
        </div>
      )}

      {factors && factors.length > 0 && (
        <div>
          <div style={{ fontSize: '12px', fontWeight: '600', color: '#4a5568', marginBottom: '8px' }}>
            风险因素
          </div>
          <div style={{ display: 'flex', flexDirection: 'column', gap: '6px' }}>
            {factors.map((factor, index) => (
              <div key={index} style={{
                display: 'flex',
                justifyContent: 'space-between',
                alignItems: 'center',
                padding: '8px 12px',
                background: 'rgba(255,255,255,0.5)',
                borderRadius: '6px',
                fontSize: '12px',
              }}>
                <span style={{ color: '#4a5568' }}>{factor.description}</span>
                <span style={{
                  padding: '2px 8px',
                  borderRadius: '10px',
                  fontSize: '11px',
                  fontWeight: '500',
                  background: factor.severity === 'high' ? '#fed7d7' :
                              factor.severity === 'medium' ? '#fefcbf' : '#c6f6d5',
                  color: factor.severity === 'high' ? '#c53030' :
                         factor.severity === 'medium' ? '#975a16' : '#276749',
                }}>
                  {factor.severity === 'high' ? '高' : factor.severity === 'medium' ? '中' : '低'}
                </span>
              </div>
            ))}
          </div>
        </div>
      )}
    </div>
  )
}
