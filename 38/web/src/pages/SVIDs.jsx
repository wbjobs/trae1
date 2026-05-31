import React, { useEffect, useState } from 'react';
import { Table, Tag, Button, Input, Space, message, Card, Alert } from 'antd';
import { ReloadOutlined, WarningOutlined } from '@ant-design/icons';
import { svidAPI } from '../api.js';

export default function SVIDs() {
  const [list, setList] = useState([]);
  const [degrade, setDegrade] = useState(null);
  const [aud, setAud] = useState('default');
  const [jwt, setJwt] = useState(null);

  const load = () => {
    svidAPI.list().then((r) => setList(r.data)).catch(() => {});
    svidAPI.degrade().then((r) => setDegrade(r.data)).catch(() => {});
  };
  useEffect(() => { load(); }, []);

  const fetchJWT = () => {
    svidAPI.jwt(aud).then((r) => { setJwt(r.data); message.success('JWT fetched'); })
      .catch((e) => message.error(e?.response?.data?.error || 'Failed'));
  };

  const columns = [
    { title: 'Type', dataIndex: 'type', key: 'type', render: (v) => <Tag color={v === 'x509' ? 'geekblue' : 'green'}>{v}</Tag> },
    { title: 'SPIFFE ID', dataIndex: 'spiffe_id', key: 'spiffe_id' },
    { title: 'Serial', dataIndex: 'serial_number', key: 'serial' },
    { title: 'Issued At', dataIndex: 'issued_at', key: 'issued_at' },
    {
      title: 'Expires At', dataIndex: 'expires_at', key: 'expires_at',
      render: (v, r) => (
        <Space>
          <span style={{ color: r.expired ? '#ff4d4f' : undefined }}>{v}</span>
          {r.expired && <Tag color="red" icon={<WarningOutlined />}>EXPIRED</Tag>}
        </Space>
      ),
    },
    {
      title: 'Grace Until', dataIndex: 'grace_until', key: 'grace_until',
      render: (v) => v || '-',
    },
  ];

  return (
    <div>
      <Space style={{ marginBottom: 16 }}>
        <Button icon={<ReloadOutlined />} onClick={load}>Reload</Button>
      </Space>
      {degrade?.degraded && (
        <Alert
          type="warning"
          showIcon
          style={{ marginBottom: 16 }}
          message="SVID Degradation Active"
          description={
            <div>
              <div><strong>SPIFFE ID:</strong> {degrade.spiffe_id}</div>
              <div><strong>Expired At:</strong> {degrade.expired_at}</div>
              <div><strong>Grace Until:</strong> {degrade.grace_until}</div>
              <div style={{ marginTop: 4, fontStyle: 'italic' }}>
                SPIRE is unreachable; using cached (expired) SVID. Service calls will continue to work until the grace period expires.
                Audit entries are being recorded.
              </div>
            </div>
          }
        />
      )}
      <Table rowKey={(r) => r.type + r.spiffe_id} columns={columns} dataSource={list} />

      <Card title="Issue JWT-SVID" style={{ marginTop: 24 }}>
        <Space>
          <Input addonBefore="Audience" value={aud} onChange={(e) => setAud(e.target.value)} style={{ width: 320 }} />
          <Button type="primary" onClick={fetchJWT}>Fetch JWT</Button>
        </Space>
        {jwt && (
          <pre style={{ marginTop: 16, background: '#f5f5f5', padding: 12, borderRadius: 4, overflowX: 'auto' }}>
{JSON.stringify(jwt, null, 2)}
          </pre>
        )}
      </Card>
    </div>
  );
}
