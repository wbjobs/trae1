import React, { useState, useEffect } from 'react'
import {
  Table,
  Button,
  Space,
  Modal,
  Form,
  Input,
  Select,
  InputNumber,
  Tag,
  Popconfirm,
  message,
  Row,
  Col,
  Card,
  Tooltip,
  Checkbox,
  Dropdown,
  Menu
} from 'antd'
import {
  PlusOutlined,
  EditOutlined,
  DeleteOutlined,
  PlayCircleOutlined,
  PauseCircleOutlined,
  ThunderboltOutlined,
  ShareAltOutlined,
  DownOutlined
} from '@ant-design/icons'
import { taskApi, serverApi } from '../services/api.js'
import CronInput from '../components/CronInput.jsx'

const { TextArea } = Input

const TaskConfig = () => {
  const [tasks, setTasks] = useState([])
  const [servers, setServers] = useState([])
  const [loading, setLoading] = useState(false)
  const [modalVisible, setModalVisible] = useState(false)
  const [editingTask, setEditingTask] = useState(null)
  const [selectedRowKeys, setSelectedRowKeys] = useState([])
  const [distributeModalVisible, setDistributeModalVisible] = useState(false)
  const [distributeTask, setDistributeTask] = useState(null)
  const [selectedServers, setSelectedServers] = useState([])
  const [form] = Form.useForm()

  const fetchTasks = async () => {
    setLoading(true)
    try {
      const data = await taskApi.getAll()
      setTasks(data)
    } catch (error) {
      message.error('获取任务列表失败')
    }
    setLoading(false)
  }

  const fetchServers = async () => {
    try {
      const data = await serverApi.getAll()
      setServers(data)
    } catch (error) {
      console.error('获取服务器列表失败')
    }
  }

  useEffect(() => {
    fetchTasks()
    fetchServers()
    const interval = setInterval(fetchTasks, 10000)
    return () => clearInterval(interval)
  }, [])

  const handleAdd = () => {
    setEditingTask(null)
    form.resetFields()
    form.setFieldsValue({
      taskType: 'SHELL',
      retryCount: 3,
      retryInterval: 5,
      timeout: 300
    })
    setModalVisible(true)
  }

  const handleEdit = (record) => {
    setEditingTask(record)
    form.setFieldsValue(record)
    setModalVisible(true)
  }

  const handleDelete = async (id) => {
    try {
      await taskApi.delete(id)
      message.success('删除成功')
      fetchTasks()
    } catch (error) {
      message.error('删除失败')
    }
  }

  const handleStart = async (id) => {
    try {
      await taskApi.start(id)
      message.success('任务已启动')
      setTasks(prev => prev.map(task => 
        task.id === id ? { ...task, status: 1 } : task
      ))
    } catch (error) {
      message.error('启动任务失败')
    }
  }

  const handleStop = async (id) => {
    try {
      await taskApi.stop(id)
      message.success('任务已停止')
      setTasks(prev => prev.map(task => 
        task.id === id ? { ...task, status: 0 } : task
      ))
    } catch (error) {
      message.error('停止任务失败')
    }
  }

  const handleExecute = async (id) => {
    try {
      await taskApi.execute(id)
      message.success('任务已触发执行')
    } catch (error) {
      message.error('执行任务失败')
    }
  }

  const handleBatchStart = async () => {
    if (selectedRowKeys.length === 0) {
      message.warning('请先选择任务')
      return
    }
    try {
      await taskApi.batchStart(selectedRowKeys)
      message.success('批量执行已触发')
      setSelectedRowKeys([])
    } catch (error) {
      message.error('批量执行失败')
    }
  }

  const handleBatchStop = async () => {
    if (selectedRowKeys.length === 0) {
      message.warning('请先选择任务')
      return
    }
    try {
      for (const id of selectedRowKeys) {
        await taskApi.stop(id)
      }
      message.success('批量停止成功')
      setSelectedRowKeys([])
      fetchTasks()
    } catch (error) {
      message.error('批量停止失败')
    }
  }

  const handleDistribute = (record) => {
    setDistributeTask(record)
    setSelectedServers([])
    setDistributeModalVisible(true)
  }

  const handleDistributeSubmit = async () => {
    if (selectedServers.length === 0) {
      message.warning('请选择目标服务器')
      return
    }
    try {
      await taskApi.distribute(distributeTask.id, selectedServers)
      message.success('任务分发成功')
      setDistributeModalVisible(false)
      fetchTasks()
    } catch (error) {
      message.error('任务分发失败')
    }
  }

  const handleSubmit = async () => {
    try {
      const values = await form.validateFields()
      if (editingTask) {
        await taskApi.update(editingTask.id, values)
        message.success('更新成功')
      } else {
        await taskApi.create(values)
        message.success('创建成功')
      }
      setModalVisible(false)
      fetchTasks()
    } catch (error) {
      if (error.errorFields) {
        return
      }
      message.error('保存失败')
    }
  }

  const columns = [
    {
      title: '任务名称',
      dataIndex: 'taskName',
      key: 'taskName',
      width: 150
    },
    {
      title: '任务组',
      dataIndex: 'taskGroup',
      key: 'taskGroup',
      width: 100
    },
    {
      title: 'Cron表达式',
      dataIndex: 'cronExpression',
      key: 'cronExpression',
      width: 150
    },
    {
      title: '目标服务器',
      dataIndex: 'targetServer',
      key: 'targetServer',
      width: 120
    },
    {
      title: '任务类型',
      dataIndex: 'taskType',
      key: 'taskType',
      width: 100,
      render: (type) => <Tag color="blue">{type}</Tag>
    },
    {
      title: '状态',
      dataIndex: 'status',
      key: 'status',
      width: 80,
      render: (status) => (
        <Tag color={status === 1 ? 'green' : 'default'}>
          {status === 1 ? '运行中' : '已停止'}
        </Tag>
      )
    },
    {
      title: '上次执行结果',
      dataIndex: 'lastExecuteResult',
      key: 'lastExecuteResult',
      width: 100,
      render: (result) => {
        if (!result) return '-'
        const colorMap = { SUCCESS: 'green', FAILED: 'red' }
        return <Tag color={colorMap[result] || 'default'}>{result}</Tag>
      }
    },
    {
      title: '操作',
      key: 'action',
      width: 300,
      render: (_, record) => (
        <Space size="small">
          {record.status === 0 ? (
            <Tooltip title="启动任务">
              <Button type="link" icon={<PlayCircleOutlined />} onClick={() => handleStart(record.id)} />
            </Tooltip>
          ) : (
            <Tooltip title="停止任务">
              <Button type="link" icon={<PauseCircleOutlined />} onClick={() => handleStop(record.id)} />
            </Tooltip>
          )}
          <Tooltip title="立即执行">
            <Button type="link" icon={<ThunderboltOutlined />} onClick={() => handleExecute(record.id)} />
          </Tooltip>
          <Tooltip title="分发任务">
            <Button type="link" icon={<ShareAltOutlined />} onClick={() => handleDistribute(record)} />
          </Tooltip>
          <Button type="link" icon={<EditOutlined />} onClick={() => handleEdit(record)} />
          <Popconfirm title="确定删除？" onConfirm={() => handleDelete(record.id)}>
            <Button type="link" danger icon={<DeleteOutlined />} />
          </Popconfirm>
        </Space>
      )
    }
  ]

  const rowSelection = {
    selectedRowKeys,
    onChange: setSelectedRowKeys,
    selections: [
      Table.SELECTION_ALL,
      Table.SELECTION_INVERT
    ]
  }

  const batchMenu = (
    <Menu>
      <Menu.Item key="start" onClick={handleBatchStart}>
        <PlayCircleOutlined /> 批量执行
      </Menu.Item>
      <Menu.Item key="stop" onClick={handleBatchStop}>
        <PauseCircleOutlined /> 批量停止
      </Menu.Item>
    </Menu>
  )

  return (
    <div>
      <Row justify="space-between" style={{ marginBottom: 16 }}>
        <Col>
          <h2>任务配置</h2>
        </Col>
        <Col>
          <Space>
            {selectedRowKeys.length > 0 && (
              <Dropdown overlay={batchMenu} trigger={['click']}>
                <Button>
                  批量操作 ({selectedRowKeys.length}) <DownOutlined />
                </Button>
              </Dropdown>
            )}
            <Button type="primary" icon={<PlusOutlined />} onClick={handleAdd}>
              新建任务
            </Button>
          </Space>
        </Col>
      </Row>
      <Card>
        <Table
          rowSelection={rowSelection}
          columns={columns}
          dataSource={tasks}
          rowKey="id"
          loading={loading}
          pagination={{ pageSize: 10 }}
        />
      </Card>
      <Modal
        title={editingTask ? '编辑任务' : '新建任务'}
        open={modalVisible}
        onCancel={() => setModalVisible(false)}
        onOk={handleSubmit}
        width={700}
        destroyOnClose
      >
        <Form form={form} layout="vertical">
          <Row gutter={16}>
            <Col span={12}>
              <Form.Item name="taskName" label="任务名称" rules={[{ required: true, message: '请输入任务名称' }]}>
                <Input placeholder="请输入任务名称" />
              </Form.Item>
            </Col>
            <Col span={12}>
              <Form.Item name="taskGroup" label="任务组" rules={[{ required: true, message: '请输入任务组' }]}>
                <Input placeholder="请输入任务组" />
              </Form.Item>
            </Col>
          </Row>
          <Form.Item name="cronExpression" label="Cron表达式" rules={[{ required: true, message: '请输入Cron表达式' }]}>
            <CronInput />
          </Form.Item>
          <Row gutter={16}>
            <Col span={12}>
              <Form.Item name="taskType" label="任务类型" rules={[{ required: true }]}>
                <Select>
                  <Select.Option value="SHELL">Shell脚本</Select.Option>
                  <Select.Option value="HTTP">HTTP请求</Select.Option>
                  <Select.Option value="JAVA">Java程序</Select.Option>
                  <Select.Option value="PYTHON">Python脚本</Select.Option>
                </Select>
              </Form.Item>
            </Col>
            <Col span={12}>
              <Form.Item name="targetServer" label="目标服务器" rules={[{ required: true, message: '请选择目标服务器' }]}>
                <Select placeholder="请选择目标服务器">
                  {servers.map(server => (
                    <Select.Option key={server.id} value={server.serverName}>
                      {server.serverName} ({server.ipAddress})
                    </Select.Option>
                  ))}
                </Select>
              </Form.Item>
            </Col>
          </Row>
          <Form.Item name="executeCommand" label="执行命令" rules={[{ required: true, message: '请输入执行命令' }]}>
            <TextArea rows={3} placeholder="请输入执行命令" />
          </Form.Item>
          <Form.Item name="taskParams" label="任务参数">
            <TextArea rows={2} placeholder="请输入任务参数(JSON格式)" />
          </Form.Item>
          <Form.Item name="description" label="任务描述">
            <TextArea rows={2} placeholder="请输入任务描述" />
          </Form.Item>
          <Row gutter={16}>
            <Col span={8}>
              <Form.Item name="retryCount" label="重试次数" rules={[{ required: true }]}>
                <InputNumber min={0} max={10} style={{ width: '100%' }} />
              </Form.Item>
            </Col>
            <Col span={8}>
              <Form.Item name="retryInterval" label="重试间隔(秒)" rules={[{ required: true }]}>
                <InputNumber min={1} max={3600} style={{ width: '100%' }} />
              </Form.Item>
            </Col>
            <Col span={8}>
              <Form.Item name="timeout" label="超时时间(秒)" rules={[{ required: true }]}>
                <InputNumber min={1} max={86400} style={{ width: '100%' }} />
              </Form.Item>
            </Col>
          </Row>
        </Form>
      </Modal>
      <Modal
        title={`分发任务: ${distributeTask?.taskName || ''}`}
        open={distributeModalVisible}
        onCancel={() => setDistributeModalVisible(false)}
        onOk={handleDistributeSubmit}
        okText="分发"
      >
        <div style={{ marginBottom: 16 }}>
          <p>选择要分发的目标服务器，任务将在选定服务器上创建副本：</p>
        </div>
        <Checkbox.Group
          style={{ width: '100%' }}
          value={selectedServers}
          onChange={setSelectedServers}
        >
          <Row gutter={[16, 16]}>
            {servers.map(server => (
              <Col span={12} key={server.id}>
                <Checkbox value={server.serverName} disabled={server.status !== 1}>
                  <Card size="small" style={{ marginBottom: 0 }}>
                    <div><strong>{server.serverName}</strong></div>
                    <div style={{ fontSize: 12, color: '#999' }}>
                      {server.ipAddress}
                      {server.status !== 1 && <Tag color="red" style={{ marginLeft: 8 }}>离线</Tag>}
                    </div>
                  </Card>
                </Checkbox>
              </Col>
            ))}
          </Row>
        </Checkbox.Group>
      </Modal>
    </div>
  )
}

export default TaskConfig
