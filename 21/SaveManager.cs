using System;
using System.Collections.Generic;
using System.IO;
using UnityEngine;

namespace PhysicsPuzzleGame
{
    [Serializable]
    public class LevelProgress
    {
        public int levelId;
        public bool isUnlocked;
        public bool isCompleted;
        public float bestTime;
        public int bestScore;
        public int collectedCoins;
        public int starsEarned;
        public int attempts;
        public List<string> collectedItems = new List<string>();
    }

    [Serializable]
    public class GameSettings
    {
        public float masterVolume = 1f;
        public float musicVolume = 0.8f;
        public float sfxVolume = 1f;
        public bool isFullscreen = true;
        public int qualityLevel = 3;
        public string controlScheme = "Keyboard";
        public bool showTutorial = true;
    }

    [Serializable]
    public class AchievementEntry
    {
        public string name;
        public int value;

        public AchievementEntry() { }

        public AchievementEntry(string n, int v)
        {
            name = n;
            value = v;
        }
    }

    [Serializable]
    public class PlayerStats
    {
        public int totalCoins;
        public int totalScore;
        public int levelsCompleted;
        public float totalPlayTime;
        public int totalDeaths;
        public int totalItemsCollected;
        public List<AchievementEntry> achievements = new List<AchievementEntry>();
    }

    [Serializable]
    public class SaveData
    {
        public string saveId;
        public string saveName;
        public string lastPlayed;
        public int currentLevelId;
        public List<LevelProgress> levelProgresses = new List<LevelProgress>();
        public GameSettings settings;
        public PlayerStats stats;
    }

    public class SaveManager : MonoBehaviour
    {
        private static SaveManager _instance;
        public static SaveManager Instance
        {
            get
            {
                if (_instance == null)
                {
                    GameObject go = new GameObject("SaveManager");
                    _instance = go.AddComponent<SaveManager>();
                    DontDestroyOnLoad(go);
                }
                return _instance;
            }
        }

        private const string SAVE_FOLDER_NAME = "Saves";
        private const string SAVE_FILE_EXTENSION = ".sav";
        private const string CURRENT_SAVE_KEY = "CurrentSaveId";
        private const string SETTINGS_FILE_NAME = "settings.json";
        private const string STATS_FILE_NAME = "stats.json";

        private string _saveDirectoryPath;
        private SaveData _currentSaveData;
        private GameSettings _settings;
        private PlayerStats _playerStats;
        private Dictionary<string, SaveData> _availableSaves = new Dictionary<string, SaveData>();
        private bool _isInitialized = false;

        public SaveData CurrentSaveData => _currentSaveData;
        public GameSettings Settings => _settings;
        public PlayerStats PlayerStats => _playerStats;
        public bool IsInitialized => _isInitialized;

        public DateTime GetLastPlayedTime()
        {
            if (_currentSaveData != null && !string.IsNullOrEmpty(_currentSaveData.lastPlayed))
            {
                DateTime result;
                if (DateTime.TryParse(_currentSaveData.lastPlayed, out result))
                {
                    return result;
                }
            }
            return DateTime.MinValue;
        }

        public void SetAchievement(string name, int value)
        {
            if (_playerStats == null) return;

            for (int i = 0; i < _playerStats.achievements.Count; i++)
            {
                if (_playerStats.achievements[i].name == name)
                {
                    _playerStats.achievements[i].value = value;
                    SavePlayerStats();
                    return;
                }
            }

            _playerStats.achievements.Add(new AchievementEntry(name, value));
            SavePlayerStats();
        }

        public int GetAchievement(string name)
        {
            if (_playerStats == null) return 0;

            foreach (var entry in _playerStats.achievements)
            {
                if (entry.name == name)
                    return entry.value;
            }
            return 0;
        }

        public event Action<SaveData> OnSaveLoaded;
        public event Action<SaveData> OnSaveCreated;
        public event Action<string> OnSaveDeleted;
        public event Action<LevelProgress> OnLevelProgressUpdated;
        public event Action<GameSettings> OnSettingsChanged;
        public event Action<PlayerStats> OnStatsUpdated;

        public void Initialize()
        {
            if (_isInitialized) return;

            _saveDirectoryPath = Path.Combine(Application.persistentDataPath, SAVE_FOLDER_NAME);

            if (!Directory.Exists(_saveDirectoryPath))
            {
                Directory.CreateDirectory(_saveDirectoryPath);
            }

            LoadSettings();
            LoadPlayerStats();
            LoadAvailableSaves();
            LoadCurrentSave();

            _isInitialized = true;
        }

        private void LoadSettings()
        {
            string settingsPath = Path.Combine(_saveDirectoryPath, SETTINGS_FILE_NAME);

            if (File.Exists(settingsPath))
            {
                try
                {
                    string json = File.ReadAllText(settingsPath);
                    _settings = JsonUtility.FromJson<GameSettings>(json);
                }
                catch (Exception e)
                {
                    Debug.LogError($"加载设置失败: {e.Message}");
                    _settings = new GameSettings();
                }
            }
            else
            {
                _settings = new GameSettings();
                SaveSettings();
            }
        }

        public void SaveSettings()
        {
            string settingsPath = Path.Combine(_saveDirectoryPath, SETTINGS_FILE_NAME);

            try
            {
                string json = JsonUtility.ToJson(_settings, true);
                File.WriteAllText(settingsPath, json);
                OnSettingsChanged?.Invoke(_settings);
            }
            catch (Exception e)
            {
                Debug.LogError($"保存设置失败: {e.Message}");
            }
        }

        public void UpdateSettings(GameSettings newSettings)
        {
            _settings = newSettings;
            SaveSettings();
        }

        private void LoadPlayerStats()
        {
            string statsPath = Path.Combine(_saveDirectoryPath, STATS_FILE_NAME);

            if (File.Exists(statsPath))
            {
                try
                {
                    string json = File.ReadAllText(statsPath);
                    _playerStats = JsonUtility.FromJson<PlayerStats>(json);
                }
                catch (Exception e)
                {
                    Debug.LogError($"加载玩家统计失败: {e.Message}");
                    _playerStats = new PlayerStats();
                }
            }
            else
            {
                _playerStats = new PlayerStats();
                SavePlayerStats();
            }
        }

        public void SavePlayerStats()
        {
            string statsPath = Path.Combine(_saveDirectoryPath, STATS_FILE_NAME);

            try
            {
                string json = JsonUtility.ToJson(_playerStats, true);
                File.WriteAllText(statsPath, json);
                OnStatsUpdated?.Invoke(_playerStats);
            }
            catch (Exception e)
            {
                Debug.LogError($"保存玩家统计失败: {e.Message}");
            }
        }

        public void AddPlayerStats(int score, int coins, float playTime, bool levelCompleted)
        {
            _playerStats.totalScore += score;
            _playerStats.totalCoins += coins;
            _playerStats.totalPlayTime += playTime;

            if (levelCompleted)
            {
                _playerStats.levelsCompleted++;
            }

            SavePlayerStats();
        }

        public void RecordDeath()
        {
            _playerStats.totalDeaths++;
            SavePlayerStats();
        }

        public void RecordItemCollected()
        {
            _playerStats.totalItemsCollected++;
            SavePlayerStats();
        }

        private void LoadAvailableSaves()
        {
            _availableSaves.Clear();

            if (!Directory.Exists(_saveDirectoryPath))
                return;

            string[] saveFiles = Directory.GetFiles(_saveDirectoryPath, $"*{SAVE_FILE_EXTENSION}");

            foreach (string filePath in saveFiles)
            {
                try
                {
                    string json = File.ReadAllText(filePath);
                    SaveData saveData = JsonUtility.FromJson<SaveData>(json);
                    if (saveData != null && !string.IsNullOrEmpty(saveData.saveId))
                    {
                        _availableSaves[saveData.saveId] = saveData;
                    }
                }
                catch (Exception e)
                {
                    Debug.LogError($"加载存档失败: {filePath}\n{e.Message}");
                }
            }
        }

        private void LoadCurrentSave()
        {
            string currentSaveId = PlayerPrefs.GetString(CURRENT_SAVE_KEY, "");

            if (!string.IsNullOrEmpty(currentSaveId) && _availableSaves.ContainsKey(currentSaveId))
            {
                LoadSave(currentSaveId);
            }
        }

        public SaveData CreateNewSave(string saveName)
        {
            string saveId = Guid.NewGuid().ToString();

            SaveData newSave = new SaveData
            {
                saveId = saveId,
                saveName = saveName,
                lastPlayed = DateTime.Now.ToString("o"),
                currentLevelId = 1,
                settings = new GameSettings(),
                stats = new PlayerStats()
            };

            LevelProgress firstLevel = new LevelProgress
            {
                levelId = 1,
                isUnlocked = true,
                isCompleted = false
            };
            newSave.levelProgresses.Add(firstLevel);

            _availableSaves[saveId] = newSave;
            _currentSaveData = newSave;

            PlayerPrefs.SetString(CURRENT_SAVE_KEY, saveId);
            PlayerPrefs.Save();

            SaveCurrentSaveData();
            OnSaveCreated?.Invoke(newSave);

            return newSave;
        }

        public bool LoadSave(string saveId)
        {
            if (!_availableSaves.ContainsKey(saveId))
            {
                Debug.LogError($"存档不存在: {saveId}");
                return false;
            }

            _currentSaveData = _availableSaves[saveId];
            _currentSaveData.lastPlayed = DateTime.Now.ToString("o");

            PlayerPrefs.SetString(CURRENT_SAVE_KEY, saveId);
            PlayerPrefs.Save();

            OnSaveLoaded?.Invoke(_currentSaveData);
            return true;
        }

        public void SaveCurrentSaveData()
        {
            if (_currentSaveData == null) return;

            _currentSaveData.lastPlayed = DateTime.Now.ToString("o");

            string savePath = Path.Combine(_saveDirectoryPath, $"{_currentSaveData.saveId}{SAVE_FILE_EXTENSION}");

            try
            {
                string json = JsonUtility.ToJson(_currentSaveData, true);
                File.WriteAllText(savePath, json);

                _availableSaves[_currentSaveData.saveId] = _currentSaveData;
            }
            catch (Exception e)
            {
                Debug.LogError($"保存存档失败: {e.Message}");
            }
        }

        public bool DeleteSave(string saveId)
        {
            string savePath = Path.Combine(_saveDirectoryPath, $"{saveId}{SAVE_FILE_EXTENSION}");

            if (File.Exists(savePath))
            {
                File.Delete(savePath);
            }

            if (_availableSaves.ContainsKey(saveId))
            {
                _availableSaves.Remove(saveId);
            }

            if (_currentSaveData != null && _currentSaveData.saveId == saveId)
            {
                _currentSaveData = null;
                PlayerPrefs.DeleteKey(CURRENT_SAVE_KEY);
                PlayerPrefs.Save();
            }

            OnSaveDeleted?.Invoke(saveId);
            return true;
        }

        public LevelProgress GetLevelProgress(int levelId)
        {
            if (_currentSaveData == null) return null;

            foreach (var progress in _currentSaveData.levelProgresses)
            {
                if (progress.levelId == levelId)
                    return progress;
            }

            return null;
        }

        public LevelProgress GetOrCreateLevelProgress(int levelId)
        {
            LevelProgress progress = GetLevelProgress(levelId);
            if (progress == null && _currentSaveData != null)
            {
                progress = new LevelProgress
                {
                    levelId = levelId,
                    isUnlocked = false,
                    isCompleted = false
                };
                _currentSaveData.levelProgresses.Add(progress);
            }
            return progress;
        }

        public void UnlockLevel(int levelId)
        {
            LevelProgress progress = GetOrCreateLevelProgress(levelId);
            if (progress != null && !progress.isUnlocked)
            {
                progress.isUnlocked = true;
                SaveCurrentSaveData();
                OnLevelProgressUpdated?.Invoke(progress);
            }
        }

        public void CompleteLevel(int levelId, float completionTime, int score, int coins)
        {
            LevelProgress progress = GetOrCreateLevelProgress(levelId);
            if (progress == null) return;

            progress.isCompleted = true;
            progress.attempts++;

            if (completionTime < progress.bestTime || progress.bestTime == 0)
            {
                progress.bestTime = completionTime;
            }

            if (score > progress.bestScore)
            {
                progress.bestScore = score;
            }

            progress.collectedCoins += coins;

            LevelData levelData = LevelDataManager.Instance.GetLevelById(levelId);
            if (levelData != null)
            {
                int stars = CalculateStars(completionTime, levelData.parTime, score);
                if (stars > progress.starsEarned)
                {
                    progress.starsEarned = stars;
                }
            }

            SaveCurrentSaveData();
            OnLevelProgressUpdated?.Invoke(progress);

            int nextLevelId = LevelDataManager.Instance.GetNextLevelId(levelId);
            if (nextLevelId > 0)
            {
                UnlockLevel(nextLevelId);
            }

            AddPlayerStats(score, coins, completionTime, true);
        }

        public int CalculateStars(float completionTime, float parTime, int score)
        {
            int stars = 1;

            if (completionTime <= parTime)
            {
                stars = 2;
            }

            if (completionTime <= parTime * 0.7f && score > 100)
            {
                stars = 3;
            }

            return stars;
        }

        public bool IsLevelUnlocked(int levelId)
        {
            LevelProgress progress = GetLevelProgress(levelId);
            return progress != null && progress.isUnlocked;
        }

        public bool IsLevelCompleted(int levelId)
        {
            LevelProgress progress = GetLevelProgress(levelId);
            return progress != null && progress.isCompleted;
        }

        public int GetCurrentLevelId()
        {
            return _currentSaveData != null ? _currentSaveData.currentLevelId : 1;
        }

        public void SetCurrentLevel(int levelId)
        {
            if (_currentSaveData != null)
            {
                _currentSaveData.currentLevelId = levelId;
                SaveCurrentSaveData();
            }
        }

        public List<SaveData> GetAllSaves()
        {
            return new List<SaveData>(_availableSaves.Values);
        }

        public bool HasAnySave()
        {
            return _availableSaves.Count > 0;
        }

        public int GetTotalCompletedLevels()
        {
            int count = 0;
            if (_currentSaveData != null)
            {
                foreach (var progress in _currentSaveData.levelProgresses)
                {
                    if (progress.isCompleted) count++;
                }
            }
            return count;
        }

        public float GetTotalBestTime()
        {
            float total = 0;
            if (_currentSaveData != null)
            {
                foreach (var progress in _currentSaveData.levelProgresses)
                {
                    if (progress.isCompleted)
                    {
                        total += progress.bestTime;
                    }
                }
            }
            return total;
        }

        public int GetTotalStars()
        {
            int total = 0;
            if (_currentSaveData != null)
            {
                foreach (var progress in _currentSaveData.levelProgresses)
                {
                    total += progress.starsEarned;
                }
            }
            return total;
        }

        public int GetMaxPossibleStars()
        {
            return LevelDataManager.Instance.LevelCount * 3;
        }

        public void ResetProgress()
        {
            if (_currentSaveData != null)
            {
                foreach (var progress in _currentSaveData.levelProgresses)
                {
                    progress.isCompleted = false;
                    progress.bestTime = 0;
                    progress.bestScore = 0;
                    progress.collectedCoins = 0;
                    progress.starsEarned = 0;
                    progress.attempts = 0;
                }

                if (_currentSaveData.levelProgresses.Count > 0)
                {
                    _currentSaveData.levelProgresses[0].isUnlocked = true;
                }

                _currentSaveData.currentLevelId = 1;
                SaveCurrentSaveData();
            }
        }

        public void Cleanup()
        {
            _availableSaves.Clear();
            _currentSaveData = null;
            _settings = null;
            _playerStats = null;
            _isInitialized = false;
        }
    }
}
