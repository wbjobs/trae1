import React, { useState, useEffect } from 'react'
import {
  Table,
  Card,
  Tag,
  Select,
  Row,
  Col,
  Button,
  Modal,
  Form,
  Input,
  Space,
  Badge,
  message,
  Tooltip,
  Statistic,
  Popconfirm
} from 'antd'
import {
  BellOutlined,
  ExclamationCircleOutlined,
  CheckCircleOutlined,
  ReloadOutlined,
  WarningOutlined
} from '@ant-design/icons'
import { alertApi } from '../services/api.js'
import dayjs from 'dayjs'

const { TextArea } = Input

const TaskAlert = ({ onAlertCountChange }) => {
  const [alerts, setAlerts] = useState([])
  const [stats, setStats] = useState({})
  const [loading, setLoading] = useState(false)
  const [pagination, setPagination] = useState({ current: 1, pageSize: 10, total: 0 })
  const [filterStatus, setFilterStatus] = useState(null)
  const [filterLevel, setFilterLevel] = useState(null)
  const [handleModalVisible, setHandleModalVisible] = useState(false)
  const [currentAlert, setCurrentAlert] = useState(null)
  const [handleForm] = Form.useForm()

  const fetchAlerts = async () => {
    setLoading(true)
    try {
      const data = await alertApi.getHistory(filterStatus, filterLevel, pagination.current - 1, pagination.pageSize)
      setAlerts(data.content || [])
      setPagination(prev => ({ ...prev, total: data.totalElements || 0 }))
    } catch (error) {
      message.error('获取告警列表失败')
    }
    setLoading(false)
  }

  const fetchStats = async () => {
    try {
      const data = await alertApi.getStats()
      setStats(data)
      if (onAlertCountChange) {
        onAlertCountChange(data.activeCount || 0)
      }
    } catch (error) {
      console.error('获取告警统计失败')
    }
  }

  useEffect(() => {
    fetchAlerts()
    fetchStats()
    const interval = setInterval(() => {
      fetchStats()
      if (filterStatus === null || filterStatus === 0) {
        fetchAlerts()
      }
    }, 30000)
    return () => clearInterval(interval)
  }, [pagination.current, pagination.pageSize, filterStatus, filterLevel])

  const handleTableChange = (newPagination) => {
    setPagination(prev => ({
      ...prev,
      current: newPagination.current,
      pageSize: newPagination.pageSize
    }))
  }

  const showHandleModal = (record) => {
    setCurrentAlert(record)
    handleForm.resetFields()
    setHandleModalVisible(true)
  }

  const handleSubmit = async () => {
    try {
      const values = await handleForm.validateFields()
      await alertApi.handle(currentAlert.id, values.handleBy, values.handleRemark)
      message.success('处理成功')
      setHandleModalVisible(false)
      fetchAlerts()
      fetchStats()
    } catch (error) {
      message.error('处理失败')
    }
  }

  const handleManualCheck = async () => {
    try {
      await alertApi.check()
      message.success('告警检查已触发')
      fetchAlerts()
      fetchStats()
    } catch (error) {
      message.error('告警检查失败')
    }
  }

  const getLevelTag = (level) => {
    const levelMap = {
      1: { color: 'blue', text: '低', icon: <BellOutlined /> },
      2: { color: 'orange', text: '中', icon: <WarningOutlined /> },
      3: { color: 'red', text: '高', icon: <ExclamationCircleOutlined /> }
    }
    const config = levelMap[level] || { color: 'default', text: '未知' }
    return <Tag icon={config.icon} color={config.color}>{config.text}</Tag>
  }

  const getTypeTag = (type) => {
    const typeMap = {
      'MISSED_EXECUTION': { color: 'purple', text: '执行超时' },
      'LONG_DURATION': { color: 'cyan', text: '耗时过长' },
      'CONSECUTIVE_FAILURE': { color: 'red', text: '连续失败' }
    }
    const config = typeMap[type] || { color: 'default', text: type }
    return <Tag color={config.color}>{config.text}</Tag>
  }

  const columns = [
    {
      title: 'ID',
      dataIndex: 'id',
      key: 'id',
      width: 80
    },
    {
      title: '任务名称',
      dataIndex: 'taskName',
      key: 'taskName',
      width: 150
    },
    {
      title: '告警类型',
      dataIndex: 'alertType',
      key: 'alertType',
      width: 120,
      render: (type) => getTypeTag(type)
    },
    {
      title: '告警级别',
      dataIndex: 'alertLevel',
      key: 'alertLevel',
      width: 100,
      render: (level) => getLevelTag(level)
    },
    {
      title: '告警消息',
      dataIndex: 'alertMessage',
      key: 'alertMessage',
      ellipsis: true
    },
    {
      title: '状态',
      dataIndex: 'status',
      key: 'status',
      width: 100,
      render: (status) => (
        <Badge
          status={status === 0 ? 'error' : 'success'}
          text={status === 0 ? '未处理' : '已处理'}
        />
      )
    },
    {
      title: '创建时间',
      dataIndex: 'createTime',
      key: 'createTime',
      width: 160,
      render: (time) => time ? dayjs(time).format('YYYY-MM-DD HH:mm:ss') : '-'
    },
    {
      title: '操作',
      key: 'action',
      width: 100,
      render: (_, record) => (
        record.status === 0 ? (
          <Button type="link" onClick={() => showHandleModal(record)}>
            处理
          </Button>
        ) : (
          <Tooltip title={record.handleRemark}>
            <Tag icon={<CheckCircleOutlined />} color="success">
              {record.handleBy}
            </Tag>
          </Tooltip>
        )
      )
    }
  ]

  return (
    <div>
      <Row justify="space-between" style={{ marginBottom: 16 }}>
        <Col>
          <h2>告警管理</h2>
        </Col>
        <Col>
          <Space>
            <Select
              placeholder="状态筛选"
              style={{ width: 120 }}
              allowClear
              value={filterStatus}
              onChange={(v) => { setFilterStatus(v); setPagination(prev => ({ ...prev, current: 1 })) }}
            >
              <Select.Option value={0}>未处理</Select.Option>
              <Select.Option value={1}>已处理</Select.Option>
            </Select>
            <Select
              placeholder="级别筛选"
              style={{ width: 120 }}
              allowClear
              value={filterLevel}
              onChange={(v) => { setFilterLevel(v); setPagination(prev => ({ ...prev, current: 1 })) }}
            >
              <Select.Option value={1}>低</Select.Option>
              <Select.Option value={2}>中</Select.Option>
              <Select.Option value={3}>高</Select.Option>
            </Select>
            <Button icon={<ReloadOutlined />} onClick={fetchAlerts}>
              刷新
            </Button>
            <Button type="primary" onClick={handleManualCheck}>
              手动检查
            </Button>
          </Space>
        </Col>
      </Row>

      <Row gutter={16} style={{ marginBottom: 16 }}>
        <Col span={6}>
          <Card>
            <Statistic
              title="活跃告警"
              value={stats.activeCount || 0}
              valueStyle={{ color: '#cf1322' }}
              prefix={<BellOutlined />}
            />
          </Card>
        </Col>
        <Col span={6}>
          <Card>
            <Statistic
              title="高级别告警"
              value={stats.highCount || 0}
              valueStyle={{ color: '#cf1322' }}
              prefix={<ExclamationCircleOutlined />}
            />
          </Card>
        </Col>
        <Col span={6}>
          <Card>
            <Statistic
              title="中级别告警"
              value={stats.mediumCount || 0}
              valueStyle={{ color: '#faad14' }}
              prefix={<WarningOutlined />}
            />
          </Card>
        </Col>
        <Col span={6}>
          <Card>
            <Statistic
              title="低级别告警"
              value={stats.lowCount || 0}
              valueStyle={{ color: '#1890ff' }}
              prefix={<BellOutlined />}
            />
          </Card>
        </Col>
      </Row>

      <Card>
        <Table
          columns={columns}
          dataSource={alerts}
          rowKey="id"
          loading={loading}
          pagination={pagination}
          onChange={handleTableChange}
        />
      </Card>

      <Modal
        title="处理告警"
        open={handleModalVisible}
        onCancel={() => setHandleModalVisible(false)}
        onOk={handleSubmit}
      >
        {currentAlert && (
          <div style={{ marginBottom: 16 }}>
            <p><strong>任务:</strong> {currentAlert.taskName}</p>
            <p><strong>告警类型:</strong> {currentAlert.alertType}</p>
            <p><strong>告警消息:</strong> {currentAlert.alertMessage}</p>
            {currentAlert.alertDetail && (
              <p><strong>详情:</strong> {currentAlert.alertDetail}</p>
            )}
          </div>
        )}
        <Form form={handleForm} layout="vertical">
          <Form.Item name="handleBy" label="处理人" rules={[{ required: true, message: '请输入处理人' }]}>
            <Input placeholder="请输入处理人" />
          </Form.Item>
          <Form.Item name="handleRemark" label="处理备注">
            <TextArea rows={3} placeholder="请输入处理备注" />
          </Form.Item>
        </Form>
      </Modal>
    </div>
  )
}

export default TaskAlert
