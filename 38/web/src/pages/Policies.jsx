import React, { useEffect, useState } from 'react';
import {
  Table, Button, Space, Modal, Form, Input, Select, Switch, Tag, message, Popconfirm, Row, Col, Card,
} from 'antd';
import { PlusOutlined, EditOutlined, DeleteOutlined, ExperimentOutlined } from '@ant-design/icons';
import { policyAPI } from '../api.js';

const HTTP_METHODS = ['GET', 'POST', 'PUT', 'DELETE', 'PATCH', 'HEAD', 'OPTIONS'];

export default function Policies() {
  const [list, setList] = useState([]);
  const [open, setOpen] = useState(false);
  const [editing, setEditing] = useState(null);
  const [testOpen, setTestOpen] = useState(false);
  const [testResult, setTestResult] = useState(null);
  const [form] = Form.useForm();
  const [testForm] = Form.useForm();

  const load = () => policyAPI.list().then((r) => setList(r.data)).catch(() => {});

  useEffect(() => { load(); }, []);

  const openCreate = () => {
    setEditing(null);
    form.resetFields();
    form.setFieldsValue({
      methods: ['GET'], effect: 'allow', pathType: 'exact', enabled: true, priority: 10,
    });
    setOpen(true);
  };

  const openEdit = (rec) => {
    setEditing(rec);
    form.setFieldsValue(rec);
    setOpen(true);
  };

  const onFinish = async (values) => {
    try {
      if (editing) {
        await policyAPI.update(editing.id, values);
        message.success('Policy updated');
      } else {
        await policyAPI.create(values);
        message.success('Policy created');
      }
      setOpen(false);
      load();
    } catch (e) {
      message.error(e?.response?.data?.error || 'Request failed');
    }
  };

  const onDelete = async (id) => {
    try {
      await policyAPI.remove(id);
      message.success('Deleted');
      load();
    } catch (e) {
      message.error(e?.response?.data?.error || 'Delete failed');
    }
  };

  const onTest = async (values) => {
    try {
      const r = await policyAPI.test(values);
      setTestResult(r.data);
      message.success('Evaluated');
    } catch (e) {
      message.error(e?.response?.data?.error || 'Test failed');
    }
  };

  const columns = [
    { title: 'Name', dataIndex: 'name', key: 'name' },
    { title: 'Source', dataIndex: 'source', key: 'source', render: (v) => <Tag color="blue">{v}</Tag> },
    { title: 'Destination', dataIndex: 'destination', key: 'destination', render: (v) => <Tag color="purple">{v}</Tag> },
    { title: 'Methods', dataIndex: 'methods', key: 'methods', render: (v) => v?.map((m) => <Tag key={m}>{m}</Tag>) },
    { title: 'Path', dataIndex: 'path', key: 'path' },
    { title: 'Type', dataIndex: 'pathType', key: 'pathType' },
    { title: 'Effect', dataIndex: 'effect', key: 'effect', render: (v) => (
      <Tag color={v === 'allow' ? 'green' : 'red'}>{v}</Tag>
    ) },
    { title: 'Priority', dataIndex: 'priority', key: 'priority' },
    { title: 'Enabled', dataIndex: 'enabled', key: 'enabled', render: (v) => <Switch checked={v} disabled /> },
    {
      title: 'Actions', key: 'actions', render: (_, r) => (
        <Space>
          <Button size="small" icon={<EditOutlined />} onClick={() => openEdit(r)}>Edit</Button>
          <Popconfirm title="Delete?" onConfirm={() => onDelete(r.id)}>
            <Button size="small" danger icon={<DeleteOutlined />}>Delete</Button>
          </Popconfirm>
        </Space>
      ),
    },
  ];

  return (
    <div>
      <Space style={{ marginBottom: 16 }}>
        <Button type="primary" icon={<PlusOutlined />} onClick={openCreate}>New Policy</Button>
        <Button icon={<ExperimentOutlined />} onClick={() => { testForm.resetFields(); setTestResult(null); setTestOpen(true); }}>
          Test Policy
        </Button>
      </Space>
      <Table rowKey="id" columns={columns} dataSource={list} />

      <Modal open={open} onCancel={() => setOpen(false)} onOk={() => form.submit()} title={editing ? 'Edit Policy' : 'New Policy'} width={640}>
        <Form form={form} layout="vertical" onFinish={onFinish}>
          <Form.Item name="name" label="Name" rules={[{ required: true }]}><Input placeholder="e.g. User API access" /></Form.Item>
          <Row gutter={16}>
            <Col span={12}>
              <Form.Item name="source" label="Source SPIFFE ID" rules={[{ required: true }]}>
                <Input placeholder="spiffe://example.org/ns/svc/client" />
              </Form.Item>
            </Col>
            <Col span={12}>
              <Form.Item name="destination" label="Destination SPIFFE ID" rules={[{ required: true }]}>
                <Input placeholder="spiffe://example.org/ns/svc/user-api" />
              </Form.Item>
            </Col>
          </Row>
          <Row gutter={16}>
            <Col span={12}>
              <Form.Item name="methods" label="Methods" rules={[{ required: true }]}>
                <Select mode="multiple" options={HTTP_METHODS.map((m) => ({ value: m, label: m }))} />
              </Form.Item>
            </Col>
            <Col span={12}>
              <Form.Item name="effect" label="Effect" rules={[{ required: true }]}>
                <Select options={[{ value: 'allow', label: 'Allow' }, { value: 'deny', label: 'Deny' }]} />
              </Form.Item>
            </Col>
          </Row>
          <Row gutter={16}>
            <Col span={16}>
              <Form.Item name="path" label="Path" rules={[{ required: true }]}>
                <Input placeholder="/api/v1/users" />
              </Form.Item>
            </Col>
            <Col span={8}>
              <Form.Item name="pathType" label="Path Type" rules={[{ required: true }]}>
                <Select options={[
                  { value: 'exact', label: 'Exact' },
                  { value: 'prefix', label: 'Prefix' },
                  { value: 'regex', label: 'Regex' },
                ]} />
              </Form.Item>
            </Col>
          </Row>
          <Row gutter={16}>
            <Col span={12}>
              <Form.Item name="priority" label="Priority" rules={[{ required: true }]}>
                <Input type="number" />
              </Form.Item>
            </Col>
            <Col span={12}>
              <Form.Item name="enabled" label="Enabled" valuePropName="checked"><Switch /></Form.Item>
            </Col>
          </Row>
          <Form.Item name="description" label="Description"><Input.TextArea rows={2} /></Form.Item>
        </Form>
      </Modal>

      <Modal open={testOpen} onCancel={() => setTestOpen(false)} footer={null} title="Policy Evaluator" width={520}>
        <Form form={testForm} layout="vertical" onFinish={onTest}>
          <Row gutter={16}>
            <Col span={12}><Form.Item name="source" label="Source" rules={[{ required: true }]}><Input /></Form.Item></Col>
            <Col span={12}><Form.Item name="destination" label="Destination" rules={[{ required: true }]}><Input /></Form.Item></Col>
          </Row>
          <Row gutter={16}>
            <Col span={12}><Form.Item name="method" label="Method" rules={[{ required: true }]}>
              <Select options={HTTP_METHODS.map((m) => ({ value: m, label: m }))} />
            </Form.Item></Col>
            <Col span={12}><Form.Item name="path" label="Path" rules={[{ required: true }]}><Input /></Form.Item></Col>
          </Row>
          <Button type="primary" htmlType="submit">Evaluate</Button>
        </Form>
        {testResult && (
          <Card size="small" style={{ marginTop: 16 }} title="Result">
            <pre>{JSON.stringify(testResult, null, 2)}</pre>
          </Card>
        )}
      </Modal>
    </div>
  );
}
