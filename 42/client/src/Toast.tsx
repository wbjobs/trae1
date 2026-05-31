import React from 'react';

interface ToastProps {
	message: string;
	loading?: boolean;
}

export const Toast: React.FC<ToastProps> = ({ message, loading }) => {
	return (
		<div className="toast-container">
			<div className="toast toast-info">
				{loading && <div className="toast-spinner" />}
				<span>{message}</span>
			</div>
		</div>
	);
};
