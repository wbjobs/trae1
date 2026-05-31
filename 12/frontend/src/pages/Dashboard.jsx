import React, { useState, useEffect } from 'react'
import {
  Row,
  Col,
  Card,
  Statistic,
  Table,
  Tag,
  Progress,
  List,
  message
} from 'antd'
import {
  CheckCircleOutlined,
  CloseCircleOutlined,
  ClockCircleOutlined,
  PlayCircleOutlined,
  PauseCircleOutlined
} from '@ant-design/icons'
import { taskApi, serverApi, logApi } from '../services/api.js'
import dayjs from 'dayjs'

const Dashboard = () => {
  const [stats, setStats] = useState({
    totalTasks: 0,
    runningTasks: 0,
    stoppedTasks: 0,
    successCount: 0,
    failedCount: 0,
    totalServers: 0,
    activeServers: 0
  })
  const [recentLogs, setRecentLogs] = useState([])
  const [servers, setServers] = useState([])
  const [tasks, setTasks] = useState([])

  const fetchData = async () => {
    try {
      const [tasksData, serversData, logsData] = await Promise.all([
        taskApi.getAll(),
        serverApi.getAll(),
        logApi.getAll(0, 10)
      ])

      const runningCount = tasksData.filter(t => t.status === 1).length
      const stoppedCount = tasksData.filter(t => t.status === 0).length
      const successCount = tasksData.filter(t => t.lastExecuteResult === 'SUCCESS').length
      const failedCount = tasksData.filter(t => t.lastExecuteResult === 'FAILED').length
      const activeServers = serversData.filter(s => s.status === 1).length

      setStats({
        totalTasks: tasksData.length,
        runningTasks: runningCount,
        stoppedTasks: stoppedCount,
        successCount,
        failedCount,
        totalServers: serversData.length,
        activeServers
      })

      setTasks(tasksData)
      setServers(serversData)
      setRecentLogs(logsData.content || [])
    } catch (error) {
      message.error('获取数据失败')
    }
  }

  useEffect(() => {
    fetchData()
    const interval = setInterval(fetchData, 10000)
    return () => clearInterval(interval)
  }, [])

  const logColumns = [
    {
      title: '任务名称',
      dataIndex: 'taskName',
      key: 'taskName'
    },
    {
      title: '状态',
      dataIndex: 'executeStatus',
      key: 'executeStatus',
      render: (status) => {
        const statusMap = {
          0: <Tag color="red">失败</Tag>,
          1: <Tag color="blue">执行中</Tag>,
          2: <Tag color="green">成功</Tag>
        }
        return statusMap[status] || <Tag>未知</Tag>
      }
    },
    {
      title: '开始时间',
      dataIndex: 'startTime',
      key: 'startTime',
      render: (time) => time ? dayjs(time).format('HH:mm:ss') : '-'
    }
  ]

  const taskColumns = [
    {
      title: '任务名称',
      dataIndex: 'taskName',
      key: 'taskName'
    },
    {
      title: '状态',
      dataIndex: 'status',
      key: 'status',
      render: (status) => (
        <Tag icon={status === 1 ? <PlayCircleOutlined /> : <PauseCircleOutlined />}
             color={status === 1 ? 'green' : 'default'}>
          {status === 1 ? '运行中' : '已停止'}
        </Tag>
      )
    },
    {
      title: '上次执行',
      dataIndex: 'lastExecuteTime',
      key: 'lastExecuteTime',
      render: (time) => time ? dayjs(time).format('MM-DD HH:mm') : '-'
    }
  ]

  return (
    <div>
      <h2>状态监控</h2>
      <Row gutter={16} style={{ marginBottom: 24 }}>
        <Col span={4}>
          <Card>
            <Statistic
              title="任务总数"
              value={stats.totalTasks}
              prefix={<ClockCircleOutlined />}
            />
          </Card>
        </Col>
        <Col span={4}>
          <Card>
            <Statistic
              title="运行中"
              value={stats.runningTasks}
              valueStyle={{ color: '#3f8600' }}
              prefix={<PlayCircleOutlined />}
            />
          </Card>
        </Col>
        <Col span={4}>
          <Card>
            <Statistic
              title="已停止"
              value={stats.stoppedTasks}
              valueStyle={{ color: '#cf1322' }}
              prefix={<PauseCircleOutlined />}
            />
          </Card>
        </Col>
        <Col span={4}>
          <Card>
            <Statistic
              title="执行成功"
              value={stats.successCount}
              valueStyle={{ color: '#3f8600' }}
              prefix={<CheckCircleOutlined />}
            />
          </Card>
        </Col>
        <Col span={4}>
          <Card>
            <Statistic
              title="执行失败"
              value={stats.failedCount}
              valueStyle={{ color: '#cf1322' }}
              prefix={<CloseCircleOutlined />}
            />
          </Card>
        </Col>
        <Col span={4}>
          <Card>
            <Statistic
              title="服务器/在线"
              value={`${stats.activeServers}/${stats.totalServers}`}
              prefix={<PlayCircleOutlined />}
            />
          </Card>
        </Col>
      </Row>

      <Row gutter={16}>
        <Col span={12}>
          <Card title="任务状态" style={{ marginBottom: 16 }}>
            <Table
              columns={taskColumns}
              dataSource={tasks.slice(0, 5)}
              rowKey="id"
              pagination={false}
              size="small"
            />
          </Card>
        </Col>
        <Col span={12}>
          <Card title="最近执行记录" style={{ marginBottom: 16 }}>
            <Table
              columns={logColumns}
              dataSource={recentLogs}
              rowKey="id"
              pagination={false}
              size="small"
            />
          </Card>
        </Col>
      </Row>

      <Row gutter={16}>
        <Col span={24}>
          <Card title="服务器状态">
            <List
              grid={{ gutter: 16, column: 4 }}
              dataSource={servers}
              renderItem={server => (
                <List.Item>
                  <Card
                    size="small"
                    title={server.serverName}
                    extra={
                      <Tag color={server.status === 1 ? 'green' : 'red'}>
                        {server.status === 1 ? '在线' : '离线'}
                      </Tag>
                    }
                  >
                    <div style={{ marginBottom: 8 }}>
                      <span style={{ color: '#999' }}>IP: </span>
                      {server.ipAddress}
                    </div>
                    {server.status === 1 && (
                      <>
                        <div style={{ marginBottom: 4 }}>
                          <span style={{ color: '#999' }}>CPU: </span>
                          <Progress
                            percent={server.cpuUsage || 0}
                            size="small"
                            status={server.cpuUsage > 80 ? 'exception' : 'normal'}
                          />
                        </div>
                        <div style={{ marginBottom: 4 }}>
                          <span style={{ color: '#999' }}>内存: </span>
                          <Progress
                            percent={server.memoryUsage || 0}
                            size="small"
                            status={server.memoryUsage > 80 ? 'exception' : 'normal'}
                          />
                        </div>
                        <div>
                          <span style={{ color: '#999' }}>磁盘: </span>
                          <Progress
                            percent={server.diskUsage || 0}
                            size="small"
                            status={server.diskUsage > 80 ? 'exception' : 'normal'}
                          />
                        </div>
                      </>
                    )}
                  </Card>
                </List.Item>
              )}
            />
          </Card>
        </Col>
      </Row>
    </div>
  )
}

export default Dashboard
