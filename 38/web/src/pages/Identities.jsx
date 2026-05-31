import React, { useEffect, useState } from 'react';
import { Table, Button, Space, Modal, Form, Input, message, Popconfirm } from 'antd';
import { PlusOutlined, DeleteOutlined } from '@ant-design/icons';
import { identityAPI } from '../api.js';

export default function Identities() {
  const [list, setList] = useState([]);
  const [open, setOpen] = useState(false);
  const [form] = Form.useForm();

  const load = () => identityAPI.list().then((r) => setList(r.data)).catch(() => {});
  useEffect(() => { load(); }, []);

  const onFinish = async (values) => {
    try {
      await identityAPI.create(values);
      message.success('Registered');
      setOpen(false);
      form.resetFields();
      load();
    } catch (e) {
      message.error(e?.response?.data?.error || 'Failed');
    }
  };

  const onDelete = async (id) => {
    try {
      await identityAPI.remove(encodeURIComponent(id));
      message.success('Deleted');
      load();
    } catch (e) {
      message.error(e?.response?.data?.error || 'Failed');
    }
  };

  const columns = [
    { title: 'SPIFFE ID', dataIndex: 'spiffe_id', key: 'spiffe_id' },
    { title: 'Name', dataIndex: 'name', key: 'name' },
    { title: 'Selector', dataIndex: 'selector', key: 'selector' },
    { title: 'Registered', dataIndex: 'registered_at', key: 'registered_at' },
    { title: 'Expires', dataIndex: 'expires_at', key: 'expires_at' },
    {
      title: 'Actions', key: 'a', render: (_, r) => (
        <Popconfirm title="Deregister?" onConfirm={() => onDelete(r.spiffe_id)}>
          <Button size="small" danger icon={<DeleteOutlined />}>Deregister</Button>
        </Popconfirm>
      ),
    },
  ];

  return (
    <div>
      <Space style={{ marginBottom: 16 }}>
        <Button type="primary" icon={<PlusOutlined />} onClick={() => { form.resetFields(); setOpen(true); }}>
          Register Service
        </Button>
      </Space>
      <Table rowKey="spiffe_id" columns={columns} dataSource={list} />
      <Modal open={open} onCancel={() => setOpen(false)} onOk={() => form.submit()} title="Register Service Identity">
        <Form form={form} layout="vertical" onFinish={onFinish}>
          <Form.Item name="spiffe_id" label="SPIFFE ID" rules={[{ required: true }]}>
            <Input placeholder="spiffe://example.org/ns/svc/foo" />
          </Form.Item>
          <Form.Item name="name" label="Name" rules={[{ required: true }]}><Input /></Form.Item>
          <Form.Item name="selector" label="Selector"><Input placeholder="k8s:ns:default" /></Form.Item>
          <Form.Item name="description" label="Description"><Input.TextArea rows={2} /></Form.Item>
        </Form>
      </Modal>
    </div>
  );
}
