import React, { useState } from 'react'
import { BrowserRouter, Routes, Route, Navigate } from 'react-router-dom'
import { Layout, Menu, theme, Badge } from 'antd'
import {
  ScheduleOutlined,
  FileTextOutlined,
  DashboardOutlined,
  ServerOutlined,
  BellOutlined
} from '@ant-design/icons'
import TaskConfig from './pages/TaskConfig.jsx'
import TaskLog from './pages/TaskLog.jsx'
import Dashboard from './pages/Dashboard.jsx'
import ServerManagement from './pages/ServerManagement.jsx'
import TaskAlert from './pages/TaskAlert.jsx'

const { Header, Sider, Content } = Layout

function App() {
  const [collapsed, setCollapsed] = useState(false)
  const [alertCount, setAlertCount] = useState(0)
  const {
    token: { colorBgContainer }
  } = theme.useToken()

  const menuItems = [
    {
      key: '/dashboard',
      icon: <DashboardOutlined />,
      label: '状态监控'
    },
    {
      key: '/tasks',
      icon: <ScheduleOutlined />,
      label: '任务配置'
    },
    {
      key: '/logs',
      icon: <FileTextOutlined />,
      label: '执行日志'
    },
    {
      key: '/alerts',
      icon: <BellOutlined />,
      label: alertCount > 0 ? (
        <Badge count={alertCount} size="small">
          告警管理
        </Badge>
      ) : '告警管理'
    },
    {
      key: '/servers',
      icon: <ServerOutlined />,
      label: '服务器管理'
    }
  ]

  return (
    <BrowserRouter>
      <Layout style={{ minHeight: '100vh' }}>
        <Sider collapsible collapsed={collapsed} onCollapse={setCollapsed}>
          <div style={{ height: 32, margin: 16, color: 'white', textAlign: 'center', fontSize: collapsed ? 14 : 18, fontWeight: 'bold' }}>
            {collapsed ? '任务' : '定时任务管理'}
          </div>
          <Menu theme="dark" defaultSelectedKeys={['/dashboard']} mode="inline" items={menuItems} />
        </Sider>
        <Layout>
          <Header style={{ padding: 0, background: colorBgContainer }} />
          <Content style={{ margin: '16px' }}>
            <div style={{ padding: 24, minHeight: 360, background: colorBgContainer }}>
              <Routes>
                <Route path="/" element={<Navigate to="/dashboard" replace />} />
                <Route path="/dashboard" element={<Dashboard />} />
                <Route path="/tasks" element={<TaskConfig />} />
                <Route path="/logs" element={<TaskLog />} />
                <Route path="/alerts" element={<TaskAlert onAlertCountChange={setAlertCount} />} />
                <Route path="/servers" element={<ServerManagement />} />
              </Routes>
            </div>
          </Content>
        </Layout>
      </Layout>
    </BrowserRouter>
  )
}

export default App
