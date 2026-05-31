using System;
using System.Collections.Generic;
using UnityEngine;

namespace PhysicsPuzzleGame
{
    [Serializable]
    public class ReplayFrame
    {
        public float timestamp;
        public Vector2 position;
        public Vector2 velocity;
        public bool isGrounded;
        public bool isJumping;
        public float health;
        public int score;
        public bool isAlive;
    }

    [Serializable]
    public class ReplayData
    {
        public int levelId;
        public string levelName;
        public DateTime startTime;
        public DateTime endTime;
        public float totalDuration;
        public bool wasCompleted;
        public float completionTime;
        public int finalScore;
        public int collectedItems;
        public int deathCount;
        public List<ReplayFrame> frames = new List<ReplayFrame>();
        public string deathReason;
        public Vector2 deathPosition;
    }

    public class ReplaySystem : MonoBehaviour
    {
        private static ReplaySystem _instance;
        public static ReplaySystem Instance
        {
            get
            {
                if (_instance == null)
                {
                    GameObject go = new GameObject("ReplaySystem");
                    _instance = go.AddComponent<ReplaySystem>();
                    DontDestroyOnLoad(go);
                }
                return _instance;
            }
        }

        private const int MAX_SAVED_REPLAYS = 10;
        private const float RECORD_INTERVAL = 0.05f;

        private ReplayData _currentReplay;
        private List<ReplayData> _replayHistory = new List<ReplayData>();
        private bool _isRecording = false;
        private float _lastRecordTime;

        private GameObject _replayPlayer;
        private bool _isPlaying = false;
        private float _replayStartTime;
        private int _currentFrameIndex;
        private float _replaySpeed = 1f;

        private PlayerController _trackedPlayer;
        private LineRenderer _pathRenderer;

        public ReplayData CurrentReplay => _currentReplay;
        public List<ReplayData> ReplayHistory => _replayHistory;
        public bool IsRecording => _isRecording;
        public bool IsPlaying => _isPlaying;

        public event Action<ReplayData> OnReplayStarted;
        public event Action<ReplayData> OnReplayCompleted;
        public event Action<ReplayData> OnReplaySaved;
        public event Action<ReplayData> OnReplayPlaybackStarted;
        public event Action<ReplayData> OnReplayPlaybackFinished;

        public void Initialize()
        {
            _replayHistory = new List<ReplayData>();
            LoadReplayHistory();
        }

        public void StartRecording(LevelData levelData)
        {
            _currentReplay = new ReplayData
            {
                levelId = levelData.levelId,
                levelName = levelData.levelName,
                startTime = DateTime.Now
            };

            _trackedPlayer = PlayerController.Instance;
            _isRecording = true;
            _lastRecordTime = 0;

            OnReplayStarted?.Invoke(_currentReplay);
        }

        public void StopRecording(bool wasCompleted)
        {
            if (_currentReplay == null || !_isRecording) return;

            _currentReplay.endTime = DateTime.Now;
            _currentReplay.totalDuration = (float)(_currentReplay.endTime - _currentReplay.startTime).TotalSeconds;
            _currentReplay.wasCompleted = wasCompleted;

            if (wasCompleted && _trackedPlayer != null)
            {
                _currentReplay.completionTime = _currentReplay.totalDuration;
                _currentReplay.finalScore = _trackedPlayer.CurrentScore;
            }

            _isRecording = false;

            AddReplayToHistory(_currentReplay);
            OnReplayCompleted?.Invoke(_currentReplay);
            OnReplaySaved?.Invoke(_currentReplay);
        }

        public void RecordFrame()
        {
            if (!_isRecording || _trackedPlayer == null) return;

            float currentTime = Time.time;
            if (currentTime - _lastRecordTime < RECORD_INTERVAL) return;

            _lastRecordTime = currentTime;

            ReplayFrame frame = new ReplayFrame
            {
                timestamp = currentTime,
                position = _trackedPlayer.transform.position,
                velocity = _trackedPlayer.GetComponent<Rigidbody2D>() != null
                    ? _trackedPlayer.GetComponent<Rigidbody2D>().velocity
                    : Vector2.zero,
                isGrounded = _trackedPlayer.IsGrounded,
                isJumping = _trackedPlayer.IsJumping,
                health = _trackedPlayer.CurrentHealth,
                score = _trackedPlayer.CurrentScore,
                isAlive = _trackedPlayer.IsAlive
            };

            _currentReplay.frames.Add(frame);
        }

        public void RecordDeath(string reason)
        {
            if (_currentReplay == null || _trackedPlayer == null) return;

            _currentReplay.deathReason = reason;
            _currentReplay.deathPosition = _trackedPlayer.transform.position;
            _currentReplay.deathCount++;

            RecordFrame();
        }

        public void RecordItemCollected()
        {
            if (_currentReplay != null)
            {
                _currentReplay.collectedItems++;
            }
        }

        private void Update()
        {
            if (_isRecording)
            {
                RecordFrame();
            }

            if (_isPlaying && _replayPlayer != null)
            {
                UpdateReplayPlayback();
            }
        }

        public void PlayReplay(ReplayData replayData)
        {
            if (replayData == null || replayData.frames.Count == 0) return;

            StopReplayPlayback();

            _replayPlayer = new GameObject("ReplayPlayer");
            SpriteRenderer renderer = _replayPlayer.AddComponent<SpriteRenderer>();

            Texture2D tex = new Texture2D(32, 32);
            for (int y = 0; y < 32; y++)
            {
                for (int x = 0; x < 32; x++)
                {
                    float dist = Vector2.Distance(new Vector2(x, y), new Vector2(16, 16));
                    if (dist < 14)
                    {
                        tex.SetPixel(x, y, new Color(1f, 0.5f, 0.5f));
                    }
                    else if (dist < 16)
                    {
                        tex.SetPixel(x, y, new Color(0.8f, 0.3f, 0.3f));
                    }
                    else
                    {
                        tex.SetPixel(x, y, Color.clear);
                    }
                }
            }
            tex.Apply();
            renderer.sprite = Sprite.Create(tex, new Rect(0, 0, 32, 32), new Vector2(0.5f, 0.5f));
            _replayPlayer.transform.localScale = new Vector3(0.8f, 0.8f, 1);

            CreatePathRenderer(replayData);

            _currentReplay = replayData;
            _isPlaying = true;
            _replayStartTime = Time.time;
            _currentFrameIndex = 0;

            OnReplayPlaybackStarted?.Invoke(replayData);
        }

        private void CreatePathRenderer(ReplayData replayData)
        {
            if (_pathRenderer != null)
            {
                Destroy(_pathRenderer.gameObject);
            }

            GameObject pathObj = new GameObject("ReplayPath");
            _pathRenderer = pathObj.AddComponent<LineRenderer>();
            _pathRenderer.material = new Material(Shader.Find("Sprites/Default"));
            _pathRenderer.startColor = new Color(1f, 0.5f, 0.5f, 0.8f);
            _pathRenderer.endColor = new Color(1f, 0.2f, 0.2f, 0.3f);
            _pathRenderer.startWidth = 0.1f;
            _pathRenderer.endWidth = 0.05f;
            _pathRenderer.sortingOrder = 90;
            _pathRenderer.positionCount = replayData.frames.Count;

            for (int i = 0; i < replayData.frames.Count; i++)
            {
                _pathRenderer.SetPosition(i, new Vector3(
                    replayData.frames[i].position.x,
                    replayData.frames[i].position.y,
                    0));
            }
        }

        private void UpdateReplayPlayback()
        {
            if (_currentReplay == null || _currentReplay.frames.Count == 0) return;

            float elapsedTime = (Time.time - _replayStartTime) * _replaySpeed;

            while (_currentFrameIndex < _currentReplay.frames.Count - 1)
            {
                ReplayFrame nextFrame = _currentReplay.frames[_currentFrameIndex + 1];
                if (nextFrame.timestamp <= elapsedTime)
                {
                    _currentFrameIndex++;
                }
                else
                {
                    break;
                }
            }

            if (_currentFrameIndex < _currentReplay.frames.Count)
            {
                ReplayFrame currentFrame = _currentReplay.frames[_currentFrameIndex];
                _replayPlayer.transform.position = currentFrame.position;

                if (!currentFrame.isAlive)
                {
                    SpriteRenderer renderer = _replayPlayer.GetComponent<SpriteRenderer>();
                    if (renderer != null)
                    {
                        renderer.color = new Color(1f, 0.3f, 0.3f, 0.5f);
                    }
                }
            }

            if (_currentFrameIndex >= _currentReplay.frames.Count - 1)
            {
                StopReplayPlayback();
            }
        }

        public void StopReplayPlayback()
        {
            if (_replayPlayer != null)
            {
                Destroy(_replayPlayer);
                _replayPlayer = null;
            }

            if (_pathRenderer != null)
            {
                Destroy(_pathRenderer.gameObject);
                _pathRenderer = null;
            }

            _isPlaying = false;
            OnReplayPlaybackFinished?.Invoke(_currentReplay);
        }

        public void SetReplaySpeed(float speed)
        {
            _replaySpeed = Mathf.Max(0.1f, Mathf.Min(5f, speed));
        }

        public void PauseReplay()
        {
            if (_isPlaying)
            {
                _isPlaying = false;
            }
        }

        public void ResumeReplay()
        {
            if (!_isPlaying && _currentReplay != null)
            {
                _isPlaying = true;
                _replayStartTime = Time.time - _currentReplay.frames[_currentFrameIndex].timestamp / _replaySpeed;
            }
        }

        private void AddReplayToHistory(ReplayData replay)
        {
            _replayHistory.Insert(0, replay);

            while (_replayHistory.Count > MAX_SAVED_REPLAYS)
            {
                _replayHistory.RemoveAt(_replayHistory.Count - 1);
            }

            SaveReplayHistory();
        }

        public ReplayData GetLatestReplay()
        {
            return _replayHistory.Count > 0 ? _replayHistory[0] : null;
        }

        public List<ReplayData> GetReplaysByLevel(int levelId)
        {
            List<ReplayData> levelReplays = new List<ReplayData>();
            foreach (var replay in _replayHistory)
            {
                if (replay.levelId == levelId)
                {
                    levelReplays.Add(replay);
                }
            }
            return levelReplays;
        }

        public List<ReplayData> GetFailedReplays()
        {
            List<ReplayData> failedReplays = new List<ReplayData>();
            foreach (var replay in _replayHistory)
            {
                if (!replay.wasCompleted)
                {
                    failedReplays.Add(replay);
                }
            }
            return failedReplays;
        }

        public ReplayData GetBestReplay(int levelId)
        {
            ReplayData best = null;
            float bestTime = float.MaxValue;

            foreach (var replay in _replayHistory)
            {
                if (replay.levelId == levelId && replay.wasCompleted)
                {
                    if (replay.completionTime < bestTime)
                    {
                        bestTime = replay.completionTime;
                        best = replay;
                    }
                }
            }

            return best;
        }

        public List<string> GenerateDeathAnalysis(ReplayData replay)
        {
            List<string> analysis = new List<string>();

            if (replay == null) return analysis;

            if (!string.IsNullOrEmpty(replay.deathReason))
            {
                analysis.Add($"死亡原因: {replay.deathReason}");
            }

            if (replay.frames.Count > 0)
            {
                float avgVelocity = CalculateAverageVelocity(replay);
                if (avgVelocity > 15f)
                {
                    analysis.Add("速度过快，建议减速移动");
                }

                int jumpCount = CountJumps(replay);
                int successfulJumps = CountSuccessfulJumps(replay);
                if (jumpCount > 0 && (float)successfulJumps / jumpCount < 0.5f)
                {
                    analysis.Add("跳跃时机不佳，观察轨迹预览");
                }

                int groundTime = CountGroundedFrames(replay);
                float groundRatio = (float)groundTime / replay.frames.Count;
                if (groundRatio < 0.3f)
                {
                    analysis.Add("大部分时间在空中，尝试更多地面控制");
                }

                if (replay.deathCount > 3)
                {
                    analysis.Add("死亡次数较多，建议降低难度或练习关卡");
                }
            }

            return analysis;
        }

        private float CalculateAverageVelocity(ReplayData replay)
        {
            if (replay.frames.Count < 2) return 0;

            float totalVelocity = 0;
            foreach (var frame in replay.frames)
            {
                totalVelocity += frame.velocity.magnitude;
            }

            return totalVelocity / replay.frames.Count;
        }

        private int CountJumps(ReplayData replay)
        {
            int count = 0;
            bool wasJumping = false;

            foreach (var frame in replay.frames)
            {
                if (frame.isJumping && !wasJumping)
                {
                    count++;
                }
                wasJumping = frame.isJumping;
            }

            return count;
        }

        private int CountSuccessfulJumps(ReplayData replay)
        {
            int count = 0;
            bool wasJumping = false;

            foreach (var frame in replay.frames)
            {
                if (!frame.isJumping && wasJumping && frame.isGrounded)
                {
                    count++;
                }
                wasJumping = frame.isJumping;
            }

            return count;
        }

        private int CountGroundedFrames(ReplayData replay)
        {
            int count = 0;
            foreach (var frame in replay.frames)
            {
                if (frame.isGrounded) count++;
            }
            return count;
        }

        public void ClearHistory()
        {
            _replayHistory.Clear();
            SaveReplayHistory();
        }

        private void SaveReplayHistory()
        {
            try
            {
                string path = System.IO.Path.Combine(Application.persistentDataPath, "replays.json");
                string json = JsonUtility.ToJson(new ReplayHistoryData { replays = _replayHistory }, true);
                System.IO.File.WriteAllText(path, json);
            }
            catch (Exception e)
            {
                Debug.LogError($"保存回放历史失败: {e.Message}");
            }
        }

        private void LoadReplayHistory()
        {
            try
            {
                string path = System.IO.Path.Combine(Application.persistentDataPath, "replays.json");
                if (System.IO.File.Exists(path))
                {
                    string json = System.IO.File.ReadAllText(path);
                    ReplayHistoryData data = JsonUtility.FromJson<ReplayHistoryData>(json);
                    if (data != null && data.replays != null)
                    {
                        _replayHistory = data.replays;
                    }
                }
            }
            catch (Exception e)
            {
                Debug.LogError($"加载回放历史失败: {e.Message}");
            }
        }

        [Serializable]
        private class ReplayHistoryData
        {
            public List<ReplayData> replays = new List<ReplayData>();
        }

        public void Cleanup()
        {
            StopReplayPlayback();
            _replayHistory.Clear();
            _isRecording = false;
        }
    }
}
