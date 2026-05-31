interface SensorSelectorProps {
  sensors: number[]
  selectedSensors: number[]
  onToggle: (sensorId: number) => void
  maxSelected?: number
}

export function SensorSelector({
  sensors,
  selectedSensors,
  onToggle,
  maxSelected = 4
}: SensorSelectorProps) {
  const isSelected = (id: number) => selectedSensors.includes(id)
  const canSelectMore = selectedSensors.length < maxSelected

  return (
    <div style={{
      background: '#121629',
      borderRadius: '8px',
      padding: '16px',
      border: '1px solid #1a1f3a'
    }}>
      <div style={{
        display: 'flex',
        justifyContent: 'space-between',
        alignItems: 'center',
        marginBottom: '12px'
      }}>
        <span style={{ color: '#888', fontSize: '14px' }}>
          选择传感器 ({selectedSensors.length}/{maxSelected})
        </span>
        {selectedSensors.length > 0 && (
          <span style={{ color: '#4ecdc4', fontSize: '12px' }}>
            已选择: {selectedSensors.sort((a, b) => a - b).join(', ')}
          </span>
        )}
      </div>
      <div style={{
        display: 'grid',
        gridTemplateColumns: 'repeat(10, 1fr)',
        gap: '4px',
        maxHeight: '200px',
        overflowY: 'auto'
      }}>
        {sensors.map((id) => {
          const selected = isSelected(id)
          const disabled = !selected && !canSelectMore
          return (
            <button
              key={id}
              onClick={() => onToggle(id)}
              disabled={disabled}
              style={{
                padding: '6px 8px',
                fontSize: '11px',
                border: selected ? '2px solid #4ecdc4' : '1px solid #2a2f4a',
                borderRadius: '4px',
                background: selected ? '#1a3a4a' : disabled ? '#0a0e1a' : '#1a1f3a',
                color: selected ? '#4ecdc4' : disabled ? '#444' : '#aaa',
                cursor: disabled ? 'not-allowed' : 'pointer',
                transition: 'all 0.2s'
              }}
              onMouseEnter={(e) => {
                if (!disabled) {
                  e.currentTarget.style.background = selected ? '#1a3a4a' : '#2a2f4a'
                }
              }}
              onMouseLeave={(e) => {
                e.currentTarget.style.background = selected ? '#1a3a4a' : disabled ? '#0a0e1a' : '#1a1f3a'
              }}
            >
              #{id}
            </button>
          )
        })}
      </div>
    </div>
  )
}
