import React, { useEffect, useState } from 'react';
import { Table, Tag, Button, Space } from 'antd';
import { ReloadOutlined } from '@ant-design/icons';
import { auditAPI } from '../api.js';

export default function Audit() {
  const [list, setList] = useState([]);
  const load = () => auditAPI.list().then((r) => setList(Array.isArray(r.data) ? r.data : [])).catch(() => {});
  useEffect(() => { load(); }, []);

  const columns = [
    { title: 'Time', dataIndex: 'time', key: 'time', width: 180 },
    { title: 'Action', dataIndex: 'action', key: 'action', render: (v) => <Tag color="blue">{v}</Tag> },
    { title: 'Operator', dataIndex: 'operator', key: 'operator' },
    { title: 'Policy ID', dataIndex: 'policy_id', key: 'policy_id' },
    { title: 'Message', dataIndex: 'message', key: 'message' },
    {
      title: 'Details', key: 'd', render: (_, r) => (
        <details>
          <summary>View</summary>
          <pre>{JSON.stringify({ before: r.before, after: r.after }, null, 2)}</pre>
        </details>
      ),
    },
  ];

  return (
    <div>
      <Space style={{ marginBottom: 16 }}>
        <Button icon={<ReloadOutlined />} onClick={load}>Reload</Button>
      </Space>
      <Table rowKey={(r, i) => i} columns={columns} dataSource={list} />
    </div>
  );
}
