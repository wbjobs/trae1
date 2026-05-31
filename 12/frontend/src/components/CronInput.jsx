import React, { useState, useEffect, useCallback } from 'react'
import { Input, Select, Row, Col, Tooltip, Button, Alert } from 'antd'
import { QuestionCircleOutlined, CheckCircleOutlined, CloseCircleOutlined } from '@ant-design/icons'
import { taskApi } from '../services/api.js'

const CronInput = ({ value, onChange }) => {
  const [cronParts, setCronParts] = useState(value ? value.split(' ') : ['0', '0', '*', '*', '*', '?'])
  const [validationResult, setValidationResult] = useState(null)
  const [validating, setValidating] = useState(false)

  const validateCron = useCallback(async (cronExpression) => {
    if (!cronExpression || cronExpression.trim().split(' ').length !== 6) {
      setValidationResult({ valid: false, message: 'Cron表达式必须包含6个字段' })
      return
    }

    setValidating(true)
    try {
      const result = await taskApi.validateCron(cronExpression)
      setValidationResult(result)
    } catch (error) {
      setValidationResult({ valid: false, message: '校验请求失败' })
    }
    setValidating(false)
  }, [])

  useEffect(() => {
    const timer = setTimeout(() => {
      validateCron(cronParts.join(' '))
    }, 500)
    return () => clearTimeout(timer)
  }, [cronParts, validateCron])

  const handlePartChange = (index, partValue) => {
    const newParts = [...cronParts]
    newParts[index] = partValue
    setCronParts(newParts)
    const newCron = newParts.join(' ')
    onChange && onChange(newCron)
  }

  const handleInputChange = (e) => {
    const newValue = e.target.value
    const parts = newValue.split(' ')
    if (parts.length === 6) {
      setCronParts(parts)
    }
    onChange && onChange(newValue)
  }

  return (
    <div>
      <Input
        value={cronParts.join(' ')}
        onChange={handleInputChange}
        placeholder="Cron表达式，如: 0 0/5 * * * ?"
      />
      {validationResult && (
        <div style={{ marginTop: 8 }}>
          {validationResult.valid ? (
            <Alert
              message="Cron表达式有效"
              type="success"
              showIcon
              icon={<CheckCircleOutlined />}
              style={{ padding: '4px 12px' }}
            />
          ) : (
            <Alert
              message={validationResult.message || 'Cron表达式无效'}
              type="error"
              showIcon
              icon={<CloseCircleOutlined />}
              style={{ padding: '4px 12px' }}
            />
          )}
        </div>
      )}
      <div style={{ marginTop: 8, fontSize: 12, color: '#666' }}>
        <Row gutter={8}>
          <Col span={4}>
            <Tooltip title="秒 (0-59)">
              <span>秒 <QuestionCircleOutlined /></span>
            </Tooltip>
            <Select
              size="small"
              value={cronParts[0]}
              onChange={(v) => handlePartChange(0, v)}
              style={{ width: '100%', marginTop: 4 }}
            >
              <Select.Option value="*">*</Select.Option>
              <Select.Option value="0">0</Select.Option>
              <Select.Option value="0/5">0/5</Select.Option>
              <Select.Option value="0/10">0/10</Select.Option>
              <Select.Option value="0/15">0/15</Select.Option>
              <Select.Option value="0/30">0/30</Select.Option>
            </Select>
          </Col>
          <Col span={4}>
            <Tooltip title="分 (0-59)">
              <span>分 <QuestionCircleOutlined /></span>
            </Tooltip>
            <Select
              size="small"
              value={cronParts[1]}
              onChange={(v) => handlePartChange(1, v)}
              style={{ width: '100%', marginTop: 4 }}
            >
              <Select.Option value="*">*</Select.Option>
              <Select.Option value="0">0</Select.Option>
              <Select.Option value="0/5">0/5</Select.Option>
              <Select.Option value="0/10">0/10</Select.Option>
              <Select.Option value="0/15">0/15</Select.Option>
              <Select.Option value="0/30">0/30</Select.Option>
            </Select>
          </Col>
          <Col span={4}>
            <Tooltip title="时 (0-23)">
              <span>时 <QuestionCircleOutlined /></span>
            </Tooltip>
            <Select
              size="small"
              value={cronParts[2]}
              onChange={(v) => handlePartChange(2, v)}
              style={{ width: '100%', marginTop: 4 }}
            >
              <Select.Option value="*">*</Select.Option>
              <Select.Option value="0">0</Select.Option>
              <Select.Option value="8">8</Select.Option>
              <Select.Option value="12">12</Select.Option>
              <Select.Option value="18">18</Select.Option>
              <Select.Option value="0/2">0/2</Select.Option>
            </Select>
          </Col>
          <Col span={4}>
            <Tooltip title="日 (1-31)">
              <span>日 <QuestionCircleOutlined /></span>
            </Tooltip>
            <Select
              size="small"
              value={cronParts[3]}
              onChange={(v) => handlePartChange(3, v)}
              style={{ width: '100%', marginTop: 4 }}
            >
              <Select.Option value="*">*</Select.Option>
              <Select.Option value="?">?</Select.Option>
              <Select.Option value="1">1</Select.Option>
              <Select.Option value="15">15</Select.Option>
              <Select.Option value="L">L</Select.Option>
            </Select>
          </Col>
          <Col span={4}>
            <Tooltip title="月 (1-12)">
              <span>月 <QuestionCircleOutlined /></span>
            </Tooltip>
            <Select
              size="small"
              value={cronParts[4]}
              onChange={(v) => handlePartChange(4, v)}
              style={{ width: '100%', marginTop: 4 }}
            >
              <Select.Option value="*">*</Select.Option>
              <Select.Option value="1">1</Select.Option>
              <Select.Option value="6">6</Select.Option>
              <Select.Option value="12">12</Select.Option>
            </Select>
          </Col>
          <Col span={4}>
            <Tooltip title="周 (1-7, 1=周日)">
              <span>周 <QuestionCircleOutlined /></span>
            </Tooltip>
            <Select
              size="small"
              value={cronParts[5]}
              onChange={(v) => handlePartChange(5, v)}
              style={{ width: '100%', marginTop: 4 }}
            >
              <Select.Option value="?">?</Select.Option>
              <Select.Option value="*">*</Select.Option>
              <Select.Option value="2">周一</Select.Option>
              <Select.Option value="3">周二</Select.Option>
              <Select.Option value="4">周三</Select.Option>
              <Select.Option value="5">周四</Select.Option>
              <Select.Option value="6">周五</Select.Option>
              <Select.Option value="2-6">工作日</Select.Option>
              <Select.Option value="7,1">周末</Select.Option>
            </Select>
          </Col>
        </Row>
        <div style={{ marginTop: 8, color: '#999' }}>
          常用表达式: 
          <Button type="link" size="small" onClick={() => handleInputChange({ target: { value: '0 0/5 * * * ?' } })}>每5分钟</Button>
          <Button type="link" size="small" onClick={() => handleInputChange({ target: { value: '0 0 * * * ?' } })}>每小时</Button>
          <Button type="link" size="small" onClick={() => handleInputChange({ target: { value: '0 0 0 * * ?' } })}>每天零点</Button>
          <Button type="link" size="small" onClick={() => handleInputChange({ target: { value: '0 0 8 * * ?' } })}>每天8点</Button>
          <Button type="link" size="small" onClick={() => handleInputChange({ target: { value: '0 0 0 ? * 2' } })}>每周一</Button>
        </div>
      </div>
    </div>
  )
}

export default CronInput
