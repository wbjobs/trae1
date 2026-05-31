import React, { useEffect, useState } from 'react';
import { Layout, Menu, theme } from 'antd';
import {
  SafetyCertificateOutlined,
  UserOutlined,
  FileTextOutlined,
  AuditOutlined,
} from '@ant-design/icons';
import { Routes, Route, Link, useLocation } from 'react-router-dom';
import Policies from './pages/Policies.jsx';
import Identities from './pages/Identities.jsx';
import SVIDs from './pages/SVIDs.jsx';
import Audit from './pages/Audit.jsx';

const { Header, Sider, Content } = Layout;

export default function App() {
  const [collapsed, setCollapsed] = useState(false);
  const {
    token: { colorBgContainer, borderRadiusLG },
  } = theme.useToken();
  const location = useLocation();

  const [selectedKey, setSelectedKey] = useState('policies');
  useEffect(() => {
    const key = location.pathname.split('/')[1] || 'policies';
    setSelectedKey(key);
  }, [location.pathname]);

  return (
    <Layout style={{ minHeight: '100vh' }}>
      <Sider collapsible collapsed={collapsed} onCollapse={setCollapsed}>
        <div className="logo" style={{ color: '#fff', padding: 16, fontSize: collapsed ? 14 : 18, fontWeight: 'bold' }}>
          {collapsed ? 'SVID' : 'SVID Gateway'}
        </div>
        <Menu
          theme="dark"
          mode="inline"
          selectedKeys={[selectedKey]}
          items={[
            { key: 'policies', icon: <SafetyCertificateOutlined />, label: <Link to="/policies">Policies</Link> },
            { key: 'identities', icon: <UserOutlined />, label: <Link to="/identities">Identities</Link> },
            { key: 'svids', icon: <FileTextOutlined />, label: <Link to="/svids">SVIDs</Link> },
            { key: 'audit', icon: <AuditOutlined />, label: <Link to="/audit">Audit Log</Link> },
          ]}
        />
      </Sider>
      <Layout>
        <Header style={{ padding: 0, background: colorBgContainer }}>
          <div style={{ padding: '0 24px', fontSize: 18, fontWeight: 600 }}>Service Identity Gateway</div>
        </Header>
        <Content style={{ margin: 24 }}>
          <div
            style={{
              padding: 24,
              minHeight: 360,
              background: colorBgContainer,
              borderRadius: borderRadiusLG,
            }}
          >
            <Routes>
              <Route path="/" element={<Policies />} />
              <Route path="/policies" element={<Policies />} />
              <Route path="/identities" element={<Identities />} />
              <Route path="/svids" element={<SVIDs />} />
              <Route path="/audit" element={<Audit />} />
            </Routes>
          </div>
        </Content>
      </Layout>
    </Layout>
  );
}
