import React from 'react';
import { ImageFile } from '../types';

interface BatchQueueProps {
  files: ImageFile[];
  currentIndex: number;
  onRemove: (id: string) => void;
  onClear: () => void;
}

export const BatchQueue: React.FC<BatchQueueProps> = ({
  files,
  currentIndex,
  onRemove,
  onClear,
}) => {
  if (files.length === 0) {
    return null;
  }

  return (
    <div
      style={{
        backgroundColor: '#252525',
        borderTop: '1px solid #333',
        padding: '12px 16px',
      }}
    >
      <div
        style={{
          display: 'flex',
          justifyContent: 'space-between',
          alignItems: 'center',
          marginBottom: '12px',
        }}
      >
        <span style={{ fontSize: '14px', color: '#ccc' }}>
          处理队列 ({files.length}/5)
        </span>
        <button
          onClick={onClear}
          style={{
            padding: '4px 12px',
            backgroundColor: 'transparent',
            color: '#888',
            border: '1px solid #555',
            borderRadius: '4px',
            cursor: 'pointer',
            fontSize: '12px',
          }}
        >
          清空队列
        </button>
      </div>

      <div style={{ display: 'flex', gap: '12px', overflowX: 'auto' }}>
        {files.map((file, index) => (
          <div
            key={file.id}
            style={{
              position: 'relative',
              width: '100px',
              height: '100px',
              backgroundColor: index === currentIndex ? '#333' : '#1a1a1a',
              border: index === currentIndex ? '2px solid #4CAF50' : '1px solid #333',
              borderRadius: '4px',
              display: 'flex',
              flexDirection: 'column',
              alignItems: 'center',
              justifyContent: 'center',
              padding: '8px',
              flexShrink: 0,
            }}
          >
            <div style={{ fontSize: '24px', marginBottom: '4px' }}>
              {file.status === 'pending' && '⏳'}
              {file.status === 'processing' && '⚙️'}
              {file.status === 'completed' && '✅'}
              {file.status === 'error' && '❌'}
            </div>

            <div
              style={{
                fontSize: '10px',
                color: '#888',
                textAlign: 'center',
                overflow: 'hidden',
                textOverflow: 'ellipsis',
                whiteSpace: 'nowrap',
                width: '100%',
              }}
            >
              {file.name}
            </div>

            {file.status === 'processing' && (
              <div
                style={{
                  position: 'absolute',
                  bottom: 0,
                  left: 0,
                  height: '3px',
                  width: `${file.progress}%`,
                  backgroundColor: '#4CAF50',
                  transition: 'width 0.3s ease',
                }}
              />
            )}

            {file.status === 'error' && (
              <div
                style={{
                  position: 'absolute',
                  top: '-4px',
                  right: '-4px',
                  backgroundColor: '#f44336',
                  color: '#fff',
                  fontSize: '10px',
                  padding: '2px 6px',
                  borderRadius: '10px',
                }}
              >
                错误
              </div>
            )}

            {index !== currentIndex && file.status !== 'processing' && (
              <button
                onClick={() => onRemove(file.id)}
                style={{
                  position: 'absolute',
                  top: '4px',
                  right: '4px',
                  width: '16px',
                  height: '16px',
                  borderRadius: '50%',
                  backgroundColor: '#555',
                  color: '#fff',
                  border: 'none',
                  cursor: 'pointer',
                  fontSize: '10px',
                  display: 'flex',
                  alignItems: 'center',
                  justifyContent: 'center',
                  opacity: 0.7,
                }}
              >
                ×
              </button>
            )}
          </div>
        ))}

        {files.length < 5 && (
          <div
            style={{
              width: '100px',
              height: '100px',
              backgroundColor: 'transparent',
              border: '2px dashed #333',
              borderRadius: '4px',
              display: 'flex',
              alignItems: 'center',
              justifyContent: 'center',
              color: '#666',
              fontSize: '24px',
              flexShrink: 0,
            }}
          >
            +
          </div>
        )}
      </div>
    </div>
  );
};
