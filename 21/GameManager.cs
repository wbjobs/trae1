using System;
using System.Collections.Generic;
using UnityEngine;

namespace PhysicsPuzzleGame
{
    public enum GameState
    {
        Loading,
        MainMenu,
        LevelSelect,
        Playing,
        Paused,
        LevelComplete,
        GameOver
    }

    public class GameManager : MonoBehaviour
    {
        private static GameManager _instance;
        public static GameManager Instance
        {
            get
            {
                if (_instance == null)
                {
                    GameObject go = new GameObject("GameManager");
                    _instance = go.AddComponent<GameManager>();
                    DontDestroyOnLoad(go);
                }
                return _instance;
            }
        }

        private GameState _currentState;
        private GameObject _playerObject;
        private PlayerController _playerController;

        private LevelData _currentLevelData;
        private float _levelTimer;
        private bool _isTimerRunning;
        private int _currentScore;
        private bool _isLevelCompleted;

        public GameState CurrentState => _currentState;
        public PlayerController Player => _playerController;
        public LevelData CurrentLevelData => _currentLevelData;
        public float LevelTimer => _levelTimer;
        public int CurrentScore => _currentScore;
        public bool IsLevelCompleted => _isLevelCompleted;

        public event Action<GameState> OnGameStateChanged;
        public event Action<LevelData> OnLevelStarted;
        public event Action<LevelData, float, int> OnLevelCompleted;
        public event Action<LevelData> OnLevelFailed;
        public event Action<float> OnTimerUpdated;
        public event Action<int> OnScoreChanged;

        private void Start()
        {
            InitializeGame();
        }

        private void InitializeGame()
        {
            _currentState = GameState.Loading;

            LevelDataManager.Instance.Initialize();
            SaveManager.Instance.Initialize();
            ItemInteraction.Instance.Initialize();
            DifficultyAdapter.Instance.Initialize();
            TrajectoryPreview.Instance.Initialize();
            ReplaySystem.Instance.Initialize();

            ChangeState(GameState.MainMenu);
        }

        public void ChangeState(GameState newState)
        {
            if (_currentState == newState) return;

            _currentState = newState;
            OnGameStateChanged?.Invoke(newState);

            switch (newState)
            {
                case GameState.MainMenu:
                    OnEnterMainMenu();
                    break;
                case GameState.LevelSelect:
                    OnEnterLevelSelect();
                    break;
                case GameState.Playing:
                    OnEnterPlaying();
                    break;
                case GameState.Paused:
                    OnEnterPaused();
                    break;
                case GameState.LevelComplete:
                    OnEnterLevelComplete();
                    break;
                case GameState.GameOver:
                    OnEnterGameOver();
                    break;
            }
        }

        private void OnEnterMainMenu()
        {
            StopTimer();
            LevelGenerator.Instance.ClearLevel();
        }

        private void OnEnterLevelSelect()
        {
            StopTimer();
        }

        private void OnEnterPlaying()
        {
            StartTimer();
        }

        private void OnEnterPaused()
        {
            StopTimer();
            Time.timeScale = 0f;
        }

        private void OnEnterLevelComplete()
        {
            StopTimer();
            Time.timeScale = 1f;
        }

        private void OnEnterGameOver()
        {
            StopTimer();
            Time.timeScale = 1f;
        }

        public void StartGame(int levelId)
        {
            LevelData levelData = LevelDataManager.Instance.GetLevelById(levelId);
            if (levelData == null)
            {
                Debug.LogError($"关卡 {levelId} 不存在");
                return;
            }

            if (!SaveManager.Instance.IsLevelUnlocked(levelId))
            {
                Debug.LogWarning($"关卡 {levelId} 未解锁");
                return;
            }

            LevelValidator.ValidationResult validationResult = LevelValidator.Instance.ValidateLevel(levelData);
            if (!validationResult.IsValid)
            {
                Debug.LogError("关卡验证失败:");
                foreach (var error in validationResult.Errors)
                {
                    Debug.LogError($"- {error}");
                }
                return;
            }

            DifficultyAdapter.Instance.AdaptLevelData(levelData);

            _currentLevelData = levelData;
            _levelTimer = 0f;
            _currentScore = 0;
            _isLevelCompleted = false;

            levelData.ResetItemStates();
            GenerateLevel(levelData);
            SpawnPlayer(levelData);

            DifficultyAdapter.Instance.RecordAttempt();
            DifficultyAdapter.Instance.ResetLevelStats();
            ReplaySystem.Instance.StartRecording(levelData);

            SaveManager.Instance.SetCurrentLevel(levelId);

            ChangeState(GameState.Playing);
            OnLevelStarted?.Invoke(levelData);
        }

        private void GenerateLevel(LevelData levelData)
        {
            LevelGenerator.Instance.GenerateLevel(levelData);
            GravitySimulator.Instance.Initialize(levelData);
        }

        private void SpawnPlayer(LevelData levelData)
        {
            if (_playerObject == null)
            {
                _playerObject = new GameObject("Player");
                _playerController = _playerObject.AddComponent<PlayerController>();
            }
            else
            {
                _playerObject.SetActive(true);
            }

            _playerController.SetStartPosition(levelData.playerStart.position);
            _playerController.ResetPlayer();
        }

        public void PauseGame()
        {
            if (_currentState == GameState.Playing)
            {
                ChangeState(GameState.Paused);
            }
        }

        public void ResumeGame()
        {
            if (_currentState == GameState.Paused)
            {
                ChangeState(GameState.Playing);
            }
        }

        public void RestartLevel()
        {
            if (_currentLevelData != null)
            {
                StartGame(_currentLevelData.levelId);
            }
        }

        public void CompleteLevel()
        {
            if (_isLevelCompleted) return;

            _isLevelCompleted = true;
            StopTimer();

            float completionTime = _levelTimer;
            int finalScore = CalculateFinalScore(completionTime);
            int coins = _playerController != null ? _playerController.CoinCount : 0;

            finalScore = DifficultyAdapter.Instance.CalculateAdaptedScore(finalScore);

            SaveManager.Instance.CompleteLevel(
                _currentLevelData.levelId,
                completionTime,
                finalScore,
                coins);

            DifficultyAdapter.Instance.RecordCompletion(completionTime, _currentLevelData.parTime);

            ReplaySystem.Instance.StopRecording(true);

            ChangeState(GameState.LevelComplete);
            OnLevelCompleted?.Invoke(_currentLevelData, completionTime, finalScore);
        }

        public void FailLevel()
        {
            ReplaySystem.Instance.StopRecording(false);
            DifficultyAdapter.Instance.RecordDeath();

            ChangeState(GameState.GameOver);
            OnLevelFailed?.Invoke(_currentLevelData);
        }

        public void GoToMainMenu()
        {
            ChangeState(GameState.MainMenu);
        }

        public void GoToLevelSelect()
        {
            ChangeState(GameState.LevelSelect);
        }

        public void GoToNextLevel()
        {
            if (_currentLevelData != null)
            {
                int nextLevelId = LevelDataManager.Instance.GetNextLevelId(_currentLevelData.levelId);
                if (nextLevelId > 0)
                {
                    StartGame(nextLevelId);
                }
                else
                {
                    GoToMainMenu();
                }
            }
        }

        private int CalculateFinalScore(float completionTime)
        {
            int baseScore = 1000;
            int timeBonus = 0;

            if (_currentLevelData != null)
            {
                float parTime = _currentLevelData.parTime;
                if (completionTime <= parTime)
                {
                    timeBonus = Mathf.RoundToInt((parTime - completionTime) * 10);
                }
            }

            int itemBonus = 0;
            if (_currentLevelData != null)
            {
                itemBonus = _currentLevelData.GetCollectedItemCount() * 50;
            }

            return baseScore + timeBonus + itemBonus;
        }

        public void StartTimer()
        {
            _isTimerRunning = true;
        }

        public void StopTimer()
        {
            _isTimerRunning = false;
        }

        public void AddTime(float seconds)
        {
            _levelTimer -= seconds;
            if (_levelTimer < 0) _levelTimer = 0;
        }

        public void AddScore(int score)
        {
            _currentScore += score;
            OnScoreChanged?.Invoke(_currentScore);
        }

        private void Update()
        {
            if (_isTimerRunning)
            {
                _levelTimer += Time.deltaTime;
                OnTimerUpdated?.Invoke(_levelTimer);

                if (_currentLevelData != null && _currentLevelData.timeLimit > 0)
                {
                    if (_levelTimer >= _currentLevelData.timeLimit)
                    {
                        FailLevel();
                    }
                }
            }

            if (_currentState == GameState.Playing)
            {
                CheckLevelCompletion();
                CheckGameOver();
            }

            if (Input.GetKeyDown(KeyCode.Escape))
            {
                if (_currentState == GameState.Playing)
                {
                    PauseGame();
                }
                else if (_currentState == GameState.Paused)
                {
                    ResumeGame();
                }
            }

            if (Input.GetKeyDown(KeyCode.R) && _currentState == GameState.Playing)
            {
                RestartLevel();
            }
        }

        private void CheckLevelCompletion()
        {
            if (_playerController == null || _currentLevelData == null) return;

            bool winCondition = LevelValidator.Instance.CheckWinCondition(
                _playerController,
                _currentLevelData);

            if (winCondition)
            {
                CompleteLevel();
            }
        }

        private void CheckGameOver()
        {
            if (_playerController == null || _currentLevelData == null) return;

            if (!_playerController.IsAlive)
            {
                FailLevel();
            }
        }

        public bool HasNextLevel()
        {
            if (_currentLevelData == null) return false;
            return LevelDataManager.Instance.HasNextLevel(_currentLevelData.levelId);
        }

        public List<LevelData> GetAvailableLevels()
        {
            List<LevelData> availableLevels = new List<LevelData>();
            List<LevelData> allLevels = LevelDataManager.Instance.GetAllLevels();

            foreach (var level in allLevels)
            {
                if (SaveManager.Instance.IsLevelUnlocked(level.levelId))
                {
                    availableLevels.Add(level);
                }
            }

            return availableLevels;
        }

        public int GetUnlockedLevelCount()
        {
            int count = 0;
            List<LevelData> allLevels = LevelDataManager.Instance.GetAllLevels();

            foreach (var level in allLevels)
            {
                if (SaveManager.Instance.IsLevelUnlocked(level.levelId))
                {
                    count++;
                }
            }

            return count;
        }

        public void UnlockAllLevels()
        {
            List<LevelData> allLevels = LevelDataManager.Instance.GetAllLevels();
            foreach (var level in allLevels)
            {
                SaveManager.Instance.UnlockLevel(level.levelId);
            }
        }

        public void ResetAllProgress()
        {
            SaveManager.Instance.ResetProgress();
        }

        public void QuitGame()
        {
#if UNITY_EDITOR
            UnityEditor.EditorApplication.isPlaying = false;
#else
            Application.Quit();
#endif
        }

        private void OnApplicationQuit()
        {
            if (SaveManager.Instance != null)
            {
                SaveManager.Instance.SaveCurrentSaveData();
            }
        }

        private void OnDestroy()
        {
            if (LevelGenerator.Instance != null)
            {
                LevelGenerator.Instance.ClearLevel();
            }

            if (GravitySimulator.Instance != null)
            {
                GravitySimulator.Instance.Cleanup();
            }

            if (PhysicsCollision2D.Instance != null)
            {
                PhysicsCollision2D.Instance.Cleanup();
            }

            if (ItemInteraction.Instance != null)
            {
                ItemInteraction.Instance.Cleanup();
            }

            if (LevelValidator.Instance != null)
            {
                LevelValidator.Instance.Cleanup();
            }

            if (DifficultyAdapter.Instance != null)
            {
                DifficultyAdapter.Instance.Cleanup();
            }

            if (TrajectoryPreview.Instance != null)
            {
                TrajectoryPreview.Instance.Cleanup();
            }

            if (ReplaySystem.Instance != null)
            {
                ReplaySystem.Instance.Cleanup();
            }

            if (LevelEditor.Instance != null)
            {
                LevelEditor.Instance.Cleanup();
            }
        }

        public void SetDifficulty(DifficultyLevel level)
        {
            DifficultyAdapter.Instance.SetDifficulty(level);
        }

        public DifficultyLevel GetCurrentDifficulty()
        {
            return DifficultyAdapter.Instance.CurrentDifficulty;
        }

        public PlayerSkillLevel GetPlayerSkill()
        {
            return DifficultyAdapter.Instance.PlayerSkill;
        }

        public List<string> GetCurrentTips()
        {
            return DifficultyAdapter.Instance.GetCurrentTips();
        }

        public void ShowTrajectoryPreview(Vector2 position, Vector2 velocity, LayerMask collisionMask)
        {
            TrajectoryPreview.Instance.PredictTrajectory(position, velocity, collisionMask);
        }

        public void HideTrajectoryPreview()
        {
            TrajectoryPreview.Instance.HidePreview();
        }

        public ReplayData GetLastReplay()
        {
            return ReplaySystem.Instance.GetLatestReplay();
        }

        public void PlayLastReplay()
        {
            ReplayData replay = ReplaySystem.Instance.GetLatestReplay();
            if (replay != null)
            {
                ReplaySystem.Instance.PlayReplay(replay);
            }
        }

        public List<string> GetDeathAnalysis()
        {
            ReplayData replay = ReplaySystem.Instance.GetLatestReplay();
            if (replay != null)
            {
                return ReplaySystem.Instance.GenerateDeathAnalysis(replay);
            }
            return new List<string>();
        }

        public void StartLevelEditor(int levelId)
        {
            LevelEditor.Instance.LoadLevel(levelId);
        }

        public void CreateNewLevel(string levelName)
        {
            LevelEditor.Instance.NewLevel(levelName);
        }

        public void SaveEditedLevel()
        {
            LevelEditor.Instance.SaveLevel();
        }

        public void TestEditedLevel()
        {
            LevelEditor.Instance.TestLevel();
        }

        public void StopLevelEditor()
        {
            LevelEditor.Instance.StopEditing();
        }
    }
}
