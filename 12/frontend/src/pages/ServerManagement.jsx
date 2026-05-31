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
  Switch,
  InputNumber as ANumber
} from 'antd'
import {
  PlusOutlined,
  EditOutlined,
  DeleteOutlined,
  ReloadOutlined,
  CheckCircleOutlined,
  CloseCircleOutlined
} from '@ant-design/icons'
import { serverApi } from '../services/api.js'
import dayjs from 'dayjs'

const { TextArea, Password } = Input

const ServerManagement = () => {
  const [servers, setServers] = useState([])
  const [loading, setLoading] = useState(false)
  const [modalVisible, setModalVisible] = useState(false)
  const [editingServer, setEditingServer] = useState(null)
  const [form] = Form.useForm()

  const fetchServers = async () => {
    setLoading(true)
    try {
      const data = await serverApi.getAll()
      setServers(data)
    } catch (error) {
      message.error('获取服务器列表失败')
    }
    setLoading(false)
  }

  useEffect(() => {
    fetchServers()
    const interval = setInterval(fetchServers, 60000)
    return () => clearInterval(interval)
  }, [])

  const handleAdd = () => {
    setEditingServer(null)
    form.resetFields()
    form.setFieldsValue({
      port: 22,
      osType: 'Linux'
    })
    setModalVisible(true)
  }

  const handleEdit = (record) => {
    setEditingServer(record)
    form.setFieldsValue(record)
    setModalVisible(true)
  }

  const handleDelete = async (id) => {
    try {
      await serverApi.delete(id)
      message.success('删除成功')
      fetchServers()
    } catch (error) {
      message.error('删除失败')
    }
  }

  const handleToggleStatus = async (record) => {
    try {
      const newStatus = record.status === 1 ? 0 : 1
      await serverApi.updateStatus(record.id, newStatus)
      message.success(newStatus === 1 ? '已启用' : '已禁用')
      fetchServers()
    } catch (error) {
      message.error('操作失败')
    }
  }

  const handleSubmit = async () => {
    try {
      const values = await form.validateFields()
      if (editingServer) {
        await serverApi.update(editingServer.id, values)
        message.success('更新成功')
      } else {
        await serverApi.create(values)
        message.success('创建成功')
      }
      setModalVisible(false)
      fetchServers()
    } catch (error) {
      if (error.errorFields) {
        return
      }
      message.error('保存失败')
    }
  }

  const columns = [
    {
      title: '服务器名称',
      dataIndex: 'serverName',
      key: 'serverName',
      width: 150
    },
    {
      title: 'IP地址',
      dataIndex: 'ipAddress',
      key: 'ipAddress',
      width: 130
    },
    {
      title: '端口',
      dataIndex: 'port',
      key: 'port',
      width: 80
    },
    {
      title: '操作系统',
      dataIndex: 'osType',
      key: 'osType',
      width: 100,
      render: (type) => <Tag>{type}</Tag>
    },
    {
      title: '状态',
      dataIndex: 'status',
      key: 'status',
      width: 80,
      render: (status) => (
        <Tag icon={status === 1 ? <CheckCircleOutlined /> : <CloseCircleOutlined />}
             color={status === 1 ? 'green' : 'red'}>
          {status === 1 ? '在线' : '离线'}
        </Tag>
      )
    },
    {
      title: 'CPU',
      dataIndex: 'cpuUsage',
      key: 'cpuUsage',
      width: 100,
      render: (usage) => usage != null ? `${usage}%` : '-'
    },
    {
      title: '内存',
      dataIndex: 'memoryUsage',
      key: 'memoryUsage',
      width: 100,
      render: (usage) => usage != null ? `${usage}%` : '-'
    },
    {
      title: '磁盘',
      dataIndex: 'diskUsage',
      key: 'diskUsage',
      width: 100,
      render: (usage) => usage != null ? `${usage}%` : '-'
    },
    {
      title: '最后心跳',
      dataIndex: 'lastHeartbeat',
      key: 'lastHeartbeat',
      width: 150,
      render: (time) => time ? dayjs(time).format('MM-DD HH:mm:ss') : '-'
    },
    {
      title: '操作',
      key: 'action',
      width: 180,
      render: (_, record) => (
        <Space size="small">
          <Tooltip title={record.status === 1 ? '禁用' : '启用'}>
            <Button
              type="link"
              onClick={() => handleToggleStatus(record)}
            >
              {record.status === 1 ? '禁用' : '启用'}
            </Button>
          </Tooltip>
          <Button type="link" icon={<EditOutlined />} onClick={() => handleEdit(record)} />
          <Popconfirm title="确定删除？" onConfirm={() => handleDelete(record.id)}>
            <Button type="link" danger icon={<DeleteOutlined />} />
          </Popconfirm>
        </Space>
      )
    }
  ]

  return (
    <div>
      <Row justify="space-between" style={{ marginBottom: 16 }}>
        <Col>
          <h2>服务器管理</h2>
        </Col>
        <Col>
          <Space>
            <Button icon={<ReloadOutlined />} onClick={fetchServers}>
              刷新
            </Button>
            <Button type="primary" icon={<PlusOutlined />} onClick={handleAdd}>
              添加服务器
            </Button>
          </Space>
        </Col>
      </Row>
      <Card>
        <Table
          columns={columns}
          dataSource={servers}
          rowKey="id"
          loading={loading}
          pagination={{ pageSize: 10 }}
        />
      </Card>
      <Modal
        title={editingServer ? '编辑服务器' : '添加服务器'}
        open={modalVisible}
        onCancel={() => setModalVisible(false)}
        onOk={handleSubmit}
        width={600}
        destroyOnClose
      >
        <Form form={form} layout="vertical">
          <Row gutter={16}>
            <Col span={12}>
              <Form.Item name="serverName" label="服务器名称" rules={[{ required: true, message: '请输入服务器名称' }]}>
                <Input placeholder="请输入服务器名称" />
              </Form.Item>
            </Col>
            <Col span={12}>
              <Form.Item name="ipAddress" label="IP地址" rules={[{ required: true, message: '请输入IP地址' }]}>
                <Input placeholder="请输入IP地址" />
              </Form.Item>
            </Col>
          </Row>
          <Row gutter={16}>
            <Col span={8}>
              <Form.Item name="port" label="端口" rules={[{ required: true }]}>
                <InputNumber min={1} max={65535} style={{ width: '100%' }} />
              </Form.Item>
            </Col>
            <Col span={8}>
              <Form.Item name="osType" label="操作系统" rules={[{ required: true }]}>
                <Select>
                  <Select.Option value="Linux">Linux</Select.Option>
                  <Select.Option value="Windows">Windows</Select.Option>
                  <Select.Option value="Unix">Unix</Select.Option>
                  <Select.Option value="macOS">macOS</Select.Option>
                </Select>
              </Form.Item>
            </Col>
            <Col span={8}>
              <Form.Item name="username" label="用户名" rules={[{ required: true, message: '请输入用户名' }]}>
                <Input placeholder="请输入用户名" />
              </Form.Item>
            </Col>
          </Row>
          <Row gutter={16}>
            <Col span={12}>
              <Form.Item name="password" label="密码">
                <Password placeholder="请输入密码" />
              </Form.Item>
            </Col>
            <Col span={12}>
              <Form.Item name="tags" label="标签">
                <Input placeholder="多个标签用逗号分隔" />
              </Form.Item>
            </Col>
          </Row>
          <Form.Item name="sshKey" label="SSH密钥">
            <TextArea rows={3} placeholder="请输入SSH私钥内容" />
          </Form.Item>
          <Form.Item name="remark" label="备注">
            <TextArea rows={2} placeholder="请输入备注信息" />
          </Form.Item>
        </Form>
      </Modal>
    </div>
  )
}

export default ServerManagement
