import React, { useState, useEffect } from 'react'
import {
  Table,
  Card,
  Tag,
  Select,
  Row,
  Col,
  DatePicker,
  Button,
  Modal,
  Descriptions,
  Space,
  Input,
  message,
  Form,
  Statistic
} from 'antd'
import { ReloadOutlined, SearchOutlined, ClearOutlined, ExportOutlined } from '@ant-design/icons'
import { logApi, taskApi, statsApi } from '../services/api.js'
import dayjs from 'dayjs'

const { RangePicker } = DatePicker

const TaskLog = () => {
  const [logs, setLogs] = useState([])
  const [tasks, setTasks] = useState([])
  const [loading, setLoading] = useState(false)
  const [pagination, setPagination] = useState({ current: 1, pageSize: 10, total: 0 })
  const [filterTaskId, setFilterTaskId] = useState(null)
  const [filterStatus, setFilterStatus] = useState(null)
  const [filterTimeRange, setFilterTimeRange] = useState(null)
  const [detailVisible, setDetailVisible] = useState(false)
  const [currentLog, setCurrentLog] = useState(null)
  const [logStats, setLogStats] = useState({})

  const fetchLogs = async () => {
    setLoading(true)
    try {
      const params = {
        page: pagination.current - 1,
        size: pagination.pageSize
      }
      if (filterTaskId) {
        params.taskId = filterTaskId
      }
      const data = await logApi.search(params)
      setLogs(data.content || [])
      setPagination(prev => ({ ...prev, total: data.totalElements || 0 }))
    } catch (error) {
      message.error('获取日志列表失败')
    }
    setLoading(false)
  }

  const fetchTasks = async () => {
    try {
      const data = await taskApi.getAll()
      setTasks(data)
    } catch (error) {
      console.error('获取任务列表失败')
    }
  }

  const fetchStats = async () => {
    try {
      const data = await statsApi.getOverallStats()
      setLogStats(data)
    } catch (error) {
      console.error('获取统计数据失败')
    }
  }

  useEffect(() => {
    fetchLogs()
    fetchTasks()
    fetchStats()
  }, [pagination.current, pagination.pageSize, filterTaskId])

  const handleTableChange = (newPagination) => {
    setPagination(prev => ({
      ...prev,
      current: newPagination.current,
      pageSize: newPagination.pageSize
    }))
  }

  const showDetail = (record) => {
    setCurrentLog(record)
    setDetailVisible(true)
  }

  const handleFilter = () => {
    setPagination(prev => ({ ...prev, current: 1 }))
    fetchLogs()
  }

  const handleClearFilter = () => {
    setFilterTaskId(null)
    setFilterStatus(null)
    setFilterTimeRange(null)
    setPagination(prev => ({ ...prev, current: 1 }))
    fetchLogs()
  }

  const handleExport = () => {
    const csvContent = [
      ['ID', '任务名称', '服务器', '状态', '时长(秒)', '开始时间', '结束时间'].join(','),
      ...logs.map(log => [
        log.id,
        log.taskName,
        log.serverName,
        log.executeStatus === 2 ? '成功' : log.executeStatus === 1 ? '执行中' : '失败',
        log.duration || '',
        log.startTime ? dayjs(log.startTime).format('YYYY-MM-DD HH:mm:ss') : '',
        log.endTime ? dayjs(log.endTime).format('YYYY-MM-DD HH:mm:ss') : ''
      ].join(','))
    ].join('\n')

    const blob = new Blob(['\ufeff' + csvContent], { type: 'text/csv;charset=utf-8;' })
    const link = document.createElement('a')
    link.href = URL.createObjectURL(blob)
    link.download = `task_logs_${dayjs().format('YYYYMMDD_HHmmss')}.csv`
    link.click()
    message.success('导出成功')
  }

  const getStatusTag = (status) => {
    const statusMap = {
      0: { color: 'red', text: '执行失败' },
      1: { color: 'blue', text: '执行中' },
      2: { color: 'green', text: '执行成功' }
    }
    const config = statusMap[status] || { color: 'default', text: '未知' }
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
      title: '目标服务器',
      dataIndex: 'serverName',
      key: 'serverName',
      width: 120
    },
    {
      title: '执行状态',
      dataIndex: 'executeStatus',
      key: 'executeStatus',
      width: 100,
      render: (status) => getStatusTag(status)
    },
    {
      title: '重试次数',
      dataIndex: 'retryAttempts',
      key: 'retryAttempts',
      width: 80
    },
    {
      title: '执行时长(秒)',
      dataIndex: 'duration',
      key: 'duration',
      width: 100,
      render: (duration) => {
        if (!duration) return '-'
        if (duration > 60) {
          return <Tag color="orange">{duration}s</Tag>
        }
        return duration
      }
    },
    {
      title: '开始时间',
      dataIndex: 'startTime',
      key: 'startTime',
      width: 160,
      render: (time) => time ? dayjs(time).format('YYYY-MM-DD HH:mm:ss') : '-'
    },
    {
      title: '结束时间',
      dataIndex: 'endTime',
      key: 'endTime',
      width: 160,
      render: (time) => time ? dayjs(time).format('YYYY-MM-DD HH:mm:ss') : '-'
    },
    {
      title: '操作',
      key: 'action',
      width: 80,
      render: (_, record) => (
        <Button type="link" onClick={() => showDetail(record)}>
          详情
        </Button>
      )
    }
  ]

  return (
    <div>
      <Row justify="space-between" style={{ marginBottom: 16 }}>
        <Col>
          <h2>执行日志</h2>
        </Col>
        <Col>
          <Space>
            <Button icon={<ExportOutlined />} onClick={handleExport}>
              导出
            </Button>
            <Button icon={<ReloadOutlined />} onClick={fetchLogs}>
              刷新
            </Button>
          </Space>
        </Col>
      </Row>

      <Row gutter={16} style={{ marginBottom: 16 }}>
        <Col span={6}>
          <Card size="small">
            <Statistic title="总执行次数" value={logStats.totalExecutions || 0} />
          </Card>
        </Col>
        <Col span={6}>
          <Card size="small">
            <Statistic
              title="成功次数"
              value={logStats.successCount || 0}
              valueStyle={{ color: '#3f8600' }}
            />
          </Card>
        </Col>
        <Col span={6}>
          <Card size="small">
            <Statistic
              title="失败次数"
              value={logStats.failedCount || 0}
              valueStyle={{ color: '#cf1322' }}
            />
          </Card>
        </Col>
        <Col span={6}>
          <Card size="small">
            <Statistic
              title="成功率"
              value={logStats.totalExecutions ? 
                ((logStats.successCount || 0) / logStats.totalExecutions * 100).toFixed(1) : 0}
              suffix="%"
              valueStyle={{ color: '#1890ff' }}
            />
          </Card>
        </Col>
      </Row>

      <Card size="small" style={{ marginBottom: 16 }}>
        <Form layout="inline">
          <Form.Item label="任务">
            <Select
              placeholder="选择任务"
              style={{ width: 200 }}
              allowClear
              value={filterTaskId}
              onChange={setFilterTaskId}
            >
              {tasks.map(task => (
                <Select.Option key={task.id} value={task.id}>
                  {task.taskName}
                </Select.Option>
              ))}
            </Select>
          </Form.Item>
          <Form.Item label="状态">
            <Select
              placeholder="选择状态"
              style={{ width: 150 }}
              allowClear
              value={filterStatus}
              onChange={setFilterStatus}
            >
              <Select.Option value={0}>执行失败</Select.Option>
              <Select.Option value={1}>执行中</Select.Option>
              <Select.Option value={2}>执行成功</Select.Option>
            </Select>
          </Form.Item>
          <Form.Item label="时间范围">
            <RangePicker
              showTime
              value={filterTimeRange}
              onChange={setFilterTimeRange}
            />
          </Form.Item>
          <Form.Item>
            <Space>
              <Button
                type="primary"
                icon={<SearchOutlined />}
                onClick={handleFilter}
              >
                查询
              </Button>
              <Button
                icon={<ClearOutlined />}
                onClick={handleClearFilter}
              >
                重置
              </Button>
            </Space>
          </Form.Item>
        </Form>
      </Card>

      <Card>
        <Table
          columns={columns}
          dataSource={logs}
          rowKey="id"
          loading={loading}
          pagination={pagination}
          onChange={handleTableChange}
        />
      </Card>
      <Modal
        title="执行详情"
        open={detailVisible}
        onCancel={() => setDetailVisible(false)}
        footer={null}
        width={700}
      >
        {currentLog && (
          <Descriptions bordered column={1}>
            <Descriptions.Item label="任务ID">{currentLog.taskId}</Descriptions.Item>
            <Descriptions.Item label="任务名称">{currentLog.taskName}</Descriptions.Item>
            <Descriptions.Item label="目标服务器">{currentLog.serverName}</Descriptions.Item>
            <Descriptions.Item label="执行状态">{getStatusTag(currentLog.executeStatus)}</Descriptions.Item>
            <Descriptions.Item label="重试次数">{currentLog.retryAttempts}</Descriptions.Item>
            <Descriptions.Item label="执行时长">{currentLog.duration} 秒</Descriptions.Item>
            <Descriptions.Item label="开始时间">
              {currentLog.startTime ? dayjs(currentLog.startTime).format('YYYY-MM-DD HH:mm:ss') : '-'}
            </Descriptions.Item>
            <Descriptions.Item label="结束时间">
              {currentLog.endTime ? dayjs(currentLog.endTime).format('YYYY-MM-DD HH:mm:ss') : '-'}
            </Descriptions.Item>
            <Descriptions.Item label="执行结果">
              <Input.TextArea
                value={currentLog.executeResult}
                readOnly
                rows={4}
              />
            </Descriptions.Item>
            {currentLog.errorMessage && (
              <Descriptions.Item label="错误信息">
                <Input.TextArea
                  value={currentLog.errorMessage}
                  readOnly
                  rows={4}
                  style={{ color: 'red' }}
                />
              </Descriptions.Item>
            )}
          </Descriptions>
        )}
      </Modal>
    </div>
  )
}

export default TaskLog
