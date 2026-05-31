import React, { useState, useRef } from 'react';
import { ImageFile } from '../types';

interface FileUploaderProps {
  onFilesSelected: (files: ImageFile[]) => void;
  isProcessing: boolean;
}

export const FileUploader: React.FC<FileUploaderProps> = ({
  onFilesSelected,
  isProcessing,
}) => {
  const [isDragging, setIsDragging] = useState(false);
  const fileInputRef = useRef<HTMLInputElement>(null);

  const handleDragOver = (e: React.DragEvent) => {
    e.preventDefault();
    setIsDragging(true);
  };

  const handleDragLeave = (e: React.DragEvent) => {
    e.preventDefault();
    setIsDragging(false);
  };

  const handleDrop = (e: React.DragEvent) => {
    e.preventDefault();
    setIsDragging(false);

    const files = Array.from(e.dataTransfer.files).filter((file) =>
      isSupportedFile(file.name)
    );

    if (files.length > 0) {
      const imageFiles = files.map((file) => ({
        id: generateId(),
        name: file.name,
        file,
        rawData: null,
        processedImage: null,
        status: 'pending' as const,
        progress: 0,
        error: null,
      }));
      onFilesSelected(imageFiles);
    }
  };

  const handleFileSelect = (e: React.ChangeEvent<HTMLInputElement>) => {
    const files = Array.from(e.target.files || []).filter((file) =>
      isSupportedFile(file.name)
    );

    if (files.length > 0) {
      const imageFiles = files.map((file) => ({
        id: generateId(),
        name: file.name,
        file,
        rawData: null,
        processedImage: null,
        status: 'pending' as const,
        progress: 0,
        error: null,
      }));
      onFilesSelected(imageFiles);
    }

    if (fileInputRef.current) {
      fileInputRef.current.value = '';
    }
  };

  const handleClick = () => {
    fileInputRef.current?.click();
  };

  const isSupportedFile = (filename: string) => {
    const ext = filename.toLowerCase().split('.').pop();
    return ['dng', 'arw', 'cr2', 'cr3', 'nef', 'raf', 'rw2', 'orf', 'pef'].includes(ext || '');
  };

  const generateId = () => {
    return Math.random().toString(36).substring(2, 15);
  };

  return (
    <div
      style={{
        padding: '20px',
        border: `2px dashed ${isDragging ? '#4CAF50' : '#555'}`,
        borderRadius: '8px',
        textAlign: 'center',
        cursor: isProcessing ? 'not-allowed' : 'pointer',
        backgroundColor: isDragging ? 'rgba(76, 175, 80, 0.1)' : 'transparent',
        transition: 'all 0.3s ease',
        opacity: isProcessing ? 0.5 : 1,
        pointerEvents: isProcessing ? 'none' : 'auto',
      }}
      onDragOver={handleDragOver}
      onDragLeave={handleDragLeave}
      onDrop={handleDrop}
      onClick={handleClick}
    >
      <input
        ref={fileInputRef}
        type="file"
        accept=".dng,.arw,.cr2,.cr3,.nef,.raf,.rw2,.orf,.pef"
        multiple
        style={{ display: 'none' }}
        onChange={handleFileSelect}
        disabled={isProcessing}
      />
      <div style={{ fontSize: '48px', marginBottom: '16px' }}>📷</div>
      <div style={{ fontSize: '16px', marginBottom: '8px' }}>
        拖放RAW文件到此处或点击选择
      </div>
      <div style={{ fontSize: '12px', color: '#888' }}>
        支持格式: DNG, ARW, CR2, CR3, NEF, RAF, RW2, ORF, PEF
      </div>
      <div style={{ fontSize: '11px', color: '#666', marginTop: '8px' }}>
        最多5张图片批量处理
      </div>
    </div>
  );
};
