import React, { useState } from 'react';

function FingerprintPanel({
  isFingerprinting,
  fingerprintCount,
  matchResults,
  matchStatus,
  storedClips,
  onStartFingerprinting,
  onStopFingerprinting,
  onSaveClip,
  onMatch,
  onRefreshClips
}) {
  const [clipName, setClipName] = useState('');

  return (
    <div className="fingerprint-panel">
      <div className="panel-section">
        <h3 className="panel-title">Audio Fingerprint</h3>
        
        <div className="fingerprint-controls">
          {!isFingerprinting ? (
            <button
              className="btn btn-fingerprint"
              onClick={onStartFingerprinting}
            >
              Start Fingerprinting
            </button>
          ) : (
            <button
              className="btn btn-stop-fingerprint"
              onClick={onStopFingerprinting}
            >
              Stop Fingerprinting
            </button>
          )}
        </div>

        <div className="fingerprint-status">
          <span className={`status-dot ${isFingerprinting ? 'active' : ''}`}></span>
          {isFingerprinting ? 'Collecting...' : 'Idle'}
          <span className="fingerprint-count">
            {fingerprintCount} hashes
          </span>
        </div>
      </div>

      <div className="panel-section">
        <h3 className="panel-title">Save Clip</h3>
        
        <div className="save-controls">
          <input
            type="text"
            className="clip-name-input"
            placeholder="Enter clip name"
            value={clipName}
            onChange={(e) => setClipName(e.target.value)}
            disabled={!isFingerprinting && fingerprintCount === 0}
          />
          <button
            className="btn btn-save"
            onClick={() => {
              if (clipName.trim()) {
                onSaveClip(clipName.trim());
                setClipName('');
              }
            }}
            disabled={!clipName.trim() || fingerprintCount === 0}
          >
            Save
          </button>
        </div>
      </div>

      <div className="panel-section">
        <h3 className="panel-title">Match Database</h3>
        
        <div className="match-controls">
          <button
            className="btn btn-match"
            onClick={onMatch}
            disabled={fingerprintCount === 0 || matchStatus === 'matching'}
          >
            {matchStatus === 'matching' ? 'Matching...' : 'Find Match'}
          </button>
        </div>

        {matchStatus === 'completed' && matchResults && (
          <div className="match-results">
            <div className="match-summary">
              <strong>Query:</strong> {matchResults.query?.fingerprintCount || 0} hashes
              <br />
              <strong>Matches found:</strong> {matchResults.totalMatches || 0}
            </div>
            
            {matchResults.matches && matchResults.matches.length > 0 ? (
              <div className="match-list">
                {matchResults.matches.slice(0, 5).map((match, idx) => (
                  <div key={idx} className="match-item">
                    <div className="match-info">
                      <span className="match-name">{match.clipName}</span>
                      <span className="match-count">{match.matchCount} matches</span>
                    </div>
                    <div className="match-confidence">
                      <div className="confidence-bar">
                        <div 
                          className="confidence-fill"
                          style={{ width: `${Math.min(100, match.averageConfidence * 100)}%` }}
                        ></div>
                      </div>
                      <span>{(match.averageConfidence * 100).toFixed(1)}%</span>
                    </div>
                  </div>
                ))}
              </div>
            ) : (
              <div className="no-matches">
                No matching clips found in database.
              </div>
            )}
          </div>
        )}
      </div>

      <div className="panel-section">
        <h3 className="panel-title">Stored Clips</h3>
        
        <div className="clips-header">
          <button className="btn btn-refresh" onClick={onRefreshClips}>
            Refresh
          </button>
          <span className="clips-count">{storedClips.length} clips</span>
        </div>

        {storedClips.length > 0 ? (
          <div className="clips-list">
            {storedClips.map((clip) => (
              <div key={clip.id} className="clip-item">
                <div className="clip-info">
                  <span className="clip-name">{clip.name}</span>
                  <span className="clip-hashes">{clip.hash_count || 0} hashes</span>
                </div>
                {clip.duration > 0 && (
                  <span className="clip-duration">
                    {clip.duration.toFixed(1)}s
                  </span>
                )}
              </div>
            ))}
          </div>
        ) : (
          <div className="no-clips">
            No clips stored yet. Record and save a clip to get started.
          </div>
        )}
      </div>
    </div>
  );
}

export default FingerprintPanel;
