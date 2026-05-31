import React, { useState, useEffect } from 'react';
import { useNavigate } from 'react-router-dom';
import ThreeWayMergeView from '../components/ThreeWayMergeView';
import { mergeApi } from '../services/api';

function MergeResolvePage() {
  const navigate = useNavigate();
  const [pendingMerge, setPendingMerge] = useState(null);
  const [loading, setLoading] = useState(false);
  const [error, setError] = useState(null);
  const [success, setSuccess] = useState(null);
  const [mergeResult, setMergeResult] = useState(null);

  useEffect(() => {
    const stored = sessionStorage.getItem('pendingMerge');
    if (stored) {
      try {
        const data = JSON.parse(stored);
        setPendingMerge(data);
        setMergeResult(data);
      } catch (err) {
        console.error('Failed to parse pending merge:', err);
        navigate('/branches');
      }
    } else {
      navigate('/branches');
    }
  }, [navigate]);

  const handleResolve = async (resolutions) => {
    if (!pendingMerge) return;

    setLoading(true);
    setError(null);

    try {
      const targetBranch = sessionStorage.getItem('mergeTargetBranch') || 'main';

      const requestData = {
        merge_result: pendingMerge.merge_result,
        resolutions: resolutions,
        target_branch: targetBranch,
        message: pendingMerge.message || `Resolve conflicts merging into ${targetBranch}`,
        author: 'anonymous',
      };

      const res = await mergeApi.resolve(requestData);

      if (res.data.success) {
        setSuccess('合并成功！冲突已解决并提交。');
        sessionStorage.removeItem('pendingMerge');
        sessionStorage.removeItem('mergeTargetBranch');

        setTimeout(() => {
          navigate('/branches');
        }, 2000);
      } else {
        setError(res.data.message || '解决冲突失败');
      }
    } catch (err) {
      setError(err.response?.data?.error || '解决冲突失败');
    } finally {
      setLoading(false);
    }
  };

  const handleCancel = () => {
    sessionStorage.removeItem('pendingMerge');
    sessionStorage.removeItem('mergeTargetBranch');
    navigate('/branches');
  };

  if (!pendingMerge) {
    return (
      <div className="page-container">
        <div className="loading">
          <div className="spinner"></div>
          加载中...
        </div>
      </div>
    );
  }

  if (success) {
    return (
      <div className="page-container">
        <div className="alert alert-success" style={{ padding: '40px', textAlign: 'center' }}>
          <p style={{ fontSize: '24px', marginBottom: '16px' }}>✓ {success}</p>
          <p>正在跳转到分支管理页面...</p>
        </div>
      </div>
    );
  }

  return (
    <div style={{ background: '#f5f5f5', minHeight: '100vh', padding: '24px' }}>
      {error && (
        <div className="alert alert-error" style={{ marginBottom: '16px' }}>
          {error}
        </div>
      )}

      {loading && (
        <div className="alert alert-info" style={{ marginBottom: '16px' }}>
          <div className="spinner" style={{ display: 'inline-block', marginRight: '12px' }}></div>
          正在提交合并结果...
        </div>
      )}

      <ThreeWayMergeView
        mergeResult={mergeResult}
        onResolve={handleResolve}
        onCancel={handleCancel}
      />
    </div>
  );
}

export default MergeResolvePage;
