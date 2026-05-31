using System;
using System.Collections.Generic;
using System.IO;
using UnityEngine;

namespace PhysicsPuzzleGame
{
    public class LevelDataManager : MonoBehaviour
    {
        private static LevelDataManager _instance;
        public static LevelDataManager Instance
        {
            get
            {
                if (_instance == null)
                {
                    GameObject go = new GameObject("LevelDataManager");
                    _instance = go.AddComponent<LevelDataManager>();
                    DontDestroyOnLoad(go);
                }
                return _instance;
            }
        }

        private const string LEVELS_FOLDER_NAME = "Levels";
        private const string LEVEL_FILE_EXTENSION = ".json";
        private const string DEFAULT_LEVELS_RESOURCE = "DefaultLevels";

        private string _levelsDirectoryPath;
        private LevelCollection _levelCollection;
        private Dictionary<int, LevelData> _levelCache = new Dictionary<int, LevelData>();
        private bool _isInitialized = false;

        public LevelCollection LevelCollection => _levelCollection;
        public bool IsInitialized => _isInitialized;
        public int LevelCount => _levelCollection != null ? _levelCollection.GetLevelCount() : 0;

        public event Action<LevelData> OnLevelLoaded;
        public event Action<LevelData> OnLevelSaved;
        public event Action OnLevelsChanged;

        public void Initialize()
        {
            if (_isInitialized) return;

            _levelsDirectoryPath = Path.Combine(Application.persistentDataPath, LEVELS_FOLDER_NAME);

            if (!Directory.Exists(_levelsDirectoryPath))
            {
                Directory.CreateDirectory(_levelsDirectoryPath);
                CreateDefaultLevels();
            }

            LoadAllLevels();
            _isInitialized = true;
        }

        private void CreateDefaultLevels()
        {
            LevelCollection defaultLevels = CreateDefaultLevelCollection();
            SaveAllLevels(defaultLevels);
        }

        public LevelCollection CreateDefaultLevelCollection()
        {
            LevelCollection collection = new LevelCollection();

            collection.levels.Add(CreateLevel1());
            collection.levels.Add(CreateLevel2());
            collection.levels.Add(CreateLevel3());
            collection.levels.Add(CreateLevel4());
            collection.levels.Add(CreateLevel5());

            return collection;
        }

        private LevelData CreateLevel1()
        {
            LevelData level = new LevelData(1, "初学者之路");
            level.description = "学习基本移动和重力";
            level.levelBounds = new Vector2(20, 15);
            level.globalGravityScale = 1f;
            level.globalGravityDirection = GravityDirection.Down;
            level.timeLimit = 120f;
            level.parTime = 45;
            level.difficulty = 1;

            level.playerStart = new PlayerStartData
            {
                position = new Vector2(-8, -5),
                initialVelocity = 0
            };

            level.goal = new GoalData
            {
                position = new Vector2(8, 3),
                size = new Vector2(1.5f, 1.5f),
                requiredItems = 0
            };

            level.obstacles.Add(new ObstacleData
            {
                obstacleType = ObstacleType.StaticPlatform,
                position = new Vector2(-8, -7),
                size = new Vector2(5, 0.5f),
                rotation = 0
            });

            level.obstacles.Add(new ObstacleData
            {
                obstacleType = ObstacleType.StaticPlatform,
                position = new Vector2(0, -3),
                size = new Vector2(4, 0.5f),
                rotation = 0
            });

            level.obstacles.Add(new ObstacleData
            {
                obstacleType = ObstacleType.StaticPlatform,
                position = new Vector2(8, 0),
                size = new Vector2(5, 0.5f),
                rotation = 0
            });

            level.obstacles.Add(new ObstacleData
            {
                obstacleType = ObstacleType.StaticPlatform,
                position = new Vector2(8, 3),
                size = new Vector2(3, 0.5f),
                rotation = 0
            });

            level.items.Add(new ItemData
            {
                itemType = ItemType.Coin,
                position = new Vector2(0, -2),
                value = 10,
                isCollected = false
            });

            level.items.Add(new ItemData
            {
                itemType = ItemType.Coin,
                position = new Vector2(4, -1),
                value = 10,
                isCollected = false
            });

            return level;
        }

        private LevelData CreateLevel2()
        {
            LevelData level = new LevelData(2, "弹跳训练");
            level.description = "学习使用弹跳板";
            level.levelBounds = new Vector2(25, 20);
            level.globalGravityScale = 1f;
            level.globalGravityDirection = GravityDirection.Down;
            level.timeLimit = 150f;
            level.parTime = 60;
            level.difficulty = 2;

            level.playerStart = new PlayerStartData
            {
                position = new Vector2(-10, -8),
                initialVelocity = 0
            };

            level.goal = new GoalData
            {
                position = new Vector2(10, 8),
                size = new Vector2(1.5f, 1.5f),
                requiredItems = 2
            };

            level.obstacles.Add(new ObstacleData
            {
                obstacleType = ObstacleType.StaticPlatform,
                position = new Vector2(-10, -10),
                size = new Vector2(6, 0.5f),
                rotation = 0
            });

            level.obstacles.Add(new ObstacleData
            {
                obstacleType = ObstacleType.BouncingPad,
                position = new Vector2(-5, -9),
                size = new Vector2(2, 0.5f),
                bounceForce = 15f
            });

            level.obstacles.Add(new ObstacleData
            {
                obstacleType = ObstacleType.StaticPlatform,
                position = new Vector2(0, 0),
                size = new Vector2(3, 0.5f),
                rotation = 0
            });

            level.obstacles.Add(new ObstacleData
            {
                obstacleType = ObstacleType.BouncingPad,
                position = new Vector2(3, 1),
                size = new Vector2(2, 0.5f),
                bounceForce = 18f
            });

            level.obstacles.Add(new ObstacleData
            {
                obstacleType = ObstacleType.StaticPlatform,
                position = new Vector2(10, 6),
                size = new Vector2(6, 0.5f),
                rotation = 0
            });

            level.items.Add(new ItemData
            {
                itemType = ItemType.Coin,
                position = new Vector2(-5, -6),
                value = 15,
                isCollected = false
            });

            level.items.Add(new ItemData
            {
                itemType = ItemType.Key,
                position = new Vector2(3, 5),
                value = 1,
                isCollected = false
            });

            return level;
        }

        private LevelData CreateLevel3()
        {
            LevelData level = new LevelData(3, "移动平台");
            level.description = "学习与移动平台交互";
            level.levelBounds = new Vector2(30, 20);
            level.globalGravityScale = 1f;
            level.globalGravityDirection = GravityDirection.Down;
            level.timeLimit = 180f;
            level.parTime = 75;
            level.difficulty = 3;

            level.playerStart = new PlayerStartData
            {
                position = new Vector2(-12, -8),
                initialVelocity = 0
            };

            level.goal = new GoalData
            {
                position = new Vector2(12, 8),
                size = new Vector2(1.5f, 1.5f),
                requiredItems = 3
            };

            level.obstacles.Add(new ObstacleData
            {
                obstacleType = ObstacleType.StaticPlatform,
                position = new Vector2(-12, -10),
                size = new Vector2(5, 0.5f),
                rotation = 0
            });

            level.obstacles.Add(new ObstacleData
            {
                obstacleType = ObstacleType.MovingPlatform,
                position = new Vector2(-5, -5),
                size = new Vector2(3, 0.5f),
                moveSpeed = 2f,
                moveRange = new Vector2(5, 0)
            });

            level.obstacles.Add(new ObstacleData
            {
                obstacleType = ObstacleType.StaticPlatform,
                position = new Vector2(5, 0),
                size = new Vector2(3, 0.5f),
                rotation = 0
            });

            level.obstacles.Add(new ObstacleData
            {
                obstacleType = ObstacleType.MovingPlatform,
                position = new Vector2(8, 3),
                size = new Vector2(3, 0.5f),
                moveSpeed = 1.5f,
                moveRange = new Vector2(0, 4)
            });

            level.obstacles.Add(new ObstacleData
            {
                obstacleType = ObstacleType.StaticPlatform,
                position = new Vector2(12, 6),
                size = new Vector2(5, 0.5f),
                rotation = 0
            });

            level.items.Add(new ItemData
            {
                itemType = ItemType.Coin,
                position = new Vector2(-5, -3),
                value = 20,
                isCollected = false
            });

            level.items.Add(new ItemData
            {
                itemType = ItemType.Coin,
                position = new Vector2(8, 5),
                value = 20,
                isCollected = false
            });

            level.items.Add(new ItemData
            {
                itemType = ItemType.Key,
                position = new Vector2(12, 6),
                value = 1,
                isCollected = false
            });

            return level;
        }

        private LevelData CreateLevel4()
        {
            LevelData level = new LevelData(4, "重力挑战");
            level.description = "学习在不同重力区域中移动";
            level.levelBounds = new Vector2(30, 25);
            level.globalGravityScale = 1f;
            level.globalGravityDirection = GravityDirection.Down;
            level.timeLimit = 200f;
            level.parTime = 90;
            level.difficulty = 4;

            level.playerStart = new PlayerStartData
            {
                position = new Vector2(-12, -10),
                initialVelocity = 0
            };

            level.goal = new GoalData
            {
                position = new Vector2(12, 10),
                size = new Vector2(1.5f, 1.5f),
                requiredItems = 2
            };

            level.obstacles.Add(new ObstacleData
            {
                obstacleType = ObstacleType.StaticPlatform,
                position = new Vector2(-12, -12),
                size = new Vector2(6, 0.5f),
                rotation = 0
            });

            level.obstacles.Add(new ObstacleData
            {
                obstacleType = ObstacleType.StaticPlatform,
                position = new Vector2(-5, -5),
                size = new Vector2(4, 0.5f),
                rotation = 0
            });

            level.obstacles.Add(new ObstacleData
            {
                obstacleType = ObstacleType.StaticPlatform,
                position = new Vector2(5, 0),
                size = new Vector2(4, 0.5f),
                rotation = 0
            });

            level.obstacles.Add(new ObstacleData
            {
                obstacleType = ObstacleType.StaticPlatform,
                position = new Vector2(12, 8),
                size = new Vector2(6, 0.5f),
                rotation = 0
            });

            level.gravityZones.Add(new GravityZoneData
            {
                position = new Vector2(0, 2),
                size = new Vector2(10, 8),
                gravityScale = 0.5f,
                gravityDirection = GravityDirection.Up
            });

            level.items.Add(new ItemData
            {
                itemType = ItemType.Coin,
                position = new Vector2(0, 3),
                value = 30,
                isCollected = false
            });

            level.items.Add(new ItemData
            {
                itemType = ItemType.Key,
                position = new Vector2(0, 6),
                value = 1,
                isCollected = false
            });

            level.items.Add(new ItemData
            {
                itemType = ItemType.Booster,
                position = new Vector2(-5, -3),
                value = 5,
                isCollected = false
            });

            return level;
        }

        private LevelData CreateLevel5()
        {
            LevelData level = new LevelData(5, "终极挑战");
            level.description = "综合运用所有技巧";
            level.levelBounds = new Vector2(35, 30);
            level.globalGravityScale = 1.2f;
            level.globalGravityDirection = GravityDirection.Down;
            level.timeLimit = 240f;
            level.parTime = 120;
            level.difficulty = 5;

            level.playerStart = new PlayerStartData
            {
                position = new Vector2(-15, -12),
                initialVelocity = 0
            };

            level.goal = new GoalData
            {
                position = new Vector2(15, 12),
                size = new Vector2(2f, 2f),
                requiredItems = 4
            };

            level.obstacles.Add(new ObstacleData
            {
                obstacleType = ObstacleType.StaticPlatform,
                position = new Vector2(-15, -14),
                size = new Vector2(6, 0.5f),
                rotation = 0
            });

            level.obstacles.Add(new ObstacleData
            {
                obstacleType = ObstacleType.BouncingPad,
                position = new Vector2(-10, -13),
                size = new Vector2(2, 0.5f),
                bounceForce = 20f
            });

            level.obstacles.Add(new ObstacleData
            {
                obstacleType = ObstacleType.MovingPlatform,
                position = new Vector2(-5, -5),
                size = new Vector2(3, 0.5f),
                moveSpeed = 2.5f,
                moveRange = new Vector2(0, 6)
            });

            level.obstacles.Add(new ObstacleData
            {
                obstacleType = ObstacleType.SpikeTrap,
                position = new Vector2(0, -2),
                size = new Vector2(4, 0.5f)
            });

            level.obstacles.Add(new ObstacleData
            {
                obstacleType = ObstacleType.StaticPlatform,
                position = new Vector2(5, 2),
                size = new Vector2(4, 0.5f),
                rotation = 0
            });

            level.obstacles.Add(new ObstacleData
            {
                obstacleType = ObstacleType.MovingPlatform,
                position = new Vector2(10, 6),
                size = new Vector2(3, 0.5f),
                moveSpeed = 2f,
                moveRange = new Vector2(6, 0)
            });

            level.obstacles.Add(new ObstacleData
            {
                obstacleType = ObstacleType.StaticPlatform,
                position = new Vector2(15, 10),
                size = new Vector2(6, 0.5f),
                rotation = 0
            });

            level.gravityZones.Add(new GravityZoneData
            {
                position = new Vector2(5, 5),
                size = new Vector2(12, 10),
                gravityScale = 0.3f,
                gravityDirection = GravityDirection.Up
            });

            level.items.Add(new ItemData
            {
                itemType = ItemType.Coin,
                position = new Vector2(-5, -2),
                value = 25,
                isCollected = false
            });

            level.items.Add(new ItemData
            {
                itemType = ItemType.Coin,
                position = new Vector2(5, 5),
                value = 25,
                isCollected = false
            });

            level.items.Add(new ItemData
            {
                itemType = ItemType.Key,
                position = new Vector2(10, 8),
                value = 1,
                isCollected = false
            });

            level.items.Add(new ItemData
            {
                itemType = ItemType.Shield,
                position = new Vector2(-10, -10),
                value = 8,
                isCollected = false
            });

            return level;
        }

        public void LoadAllLevels()
        {
            _levelCollection = new LevelCollection();
            _levelCache.Clear();

            if (!Directory.Exists(_levelsDirectoryPath))
            {
                Directory.CreateDirectory(_levelsDirectoryPath);
                return;
            }

            string[] levelFiles = Directory.GetFiles(_levelsDirectoryPath, $"*{LEVEL_FILE_EXTENSION}");

            foreach (string filePath in levelFiles)
            {
                try
                {
                    string json = File.ReadAllText(filePath);
                    LevelData level = JsonUtility.FromJson<LevelData>(json);
                    if (level != null)
                    {
                        _levelCollection.levels.Add(level);
                        _levelCache[level.levelId] = level;
                    }
                }
                catch (Exception e)
                {
                    Debug.LogError($"加载关卡文件失败: {filePath}\n{e.Message}");
                }
            }

            _levelCollection.levels.Sort((a, b) => a.levelId.CompareTo(b.levelId));
        }

        public LevelData GetLevelById(int levelId)
        {
            if (_levelCache.ContainsKey(levelId))
                return _levelCache[levelId];

            LevelData level = _levelCollection?.GetLevelById(levelId);
            if (level != null)
                _levelCache[levelId] = level;

            return level;
        }

        public LevelData GetLevelByName(string levelName)
        {
            return _levelCollection?.GetLevelByName(levelName);
        }

        public LevelData GetLevelByIndex(int index)
        {
            if (_levelCollection == null || index < 0 || index >= _levelCollection.GetLevelCount())
                return null;

            return _levelCollection.levels[index];
        }

        public List<LevelData> GetAllLevels()
        {
            return _levelCollection?.levels ?? new List<LevelData>();
        }

        public void SaveLevel(LevelData level)
        {
            if (level == null) return;

            string filePath = Path.Combine(_levelsDirectoryPath, $"Level_{level.levelId}{LEVEL_FILE_EXTENSION}");

            try
            {
                string json = JsonUtility.ToJson(level, true);
                File.WriteAllText(filePath, json);

                _levelCache[level.levelId] = level;

                if (_levelCollection.GetLevelById(level.levelId) == null)
                {
                    _levelCollection.levels.Add(level);
                }

                OnLevelSaved?.Invoke(level);
                OnLevelsChanged?.Invoke();
            }
            catch (Exception e)
            {
                Debug.LogError($"保存关卡失败: {level.levelName}\n{e.Message}");
            }
        }

        public void SaveAllLevels(LevelCollection collection)
        {
            if (collection == null) return;

            foreach (var level in collection.levels)
            {
                SaveLevel(level);
            }
        }

        public void DeleteLevel(int levelId)
        {
            string filePath = Path.Combine(_levelsDirectoryPath, $"Level_{levelId}{LEVEL_FILE_EXTENSION}");

            if (File.Exists(filePath))
            {
                File.Delete(filePath);
            }

            LevelData level = _levelCollection.GetLevelById(levelId);
            if (level != null)
            {
                _levelCollection.levels.Remove(level);
            }

            if (_levelCache.ContainsKey(levelId))
            {
                _levelCache.Remove(levelId);
            }

            OnLevelsChanged?.Invoke();
        }

        public bool LevelExists(int levelId)
        {
            string filePath = Path.Combine(_levelsDirectoryPath, $"Level_{levelId}{LEVEL_FILE_EXTENSION}");
            return File.Exists(filePath);
        }

        public LevelData CreateNewLevel(string levelName)
        {
            int newId = 1;
            while (LevelExists(newId))
            {
                newId++;
            }

            LevelData newLevel = new LevelData(newId, levelName)
            {
                levelBounds = new Vector2(20, 15),
                globalGravityScale = 1f,
                globalGravityDirection = GravityDirection.Down,
                timeLimit = 120f,
                parTime = 60,
                difficulty = 1,
                playerStart = new PlayerStartData
                {
                    position = new Vector2(-8, -5),
                    initialVelocity = 0
                },
                goal = new GoalData
                {
                    position = new Vector2(8, 3),
                    size = new Vector2(1.5f, 1.5f),
                    requiredItems = 0
                }
            };

            SaveLevel(newLevel);
            return newLevel;
        }

        public string ExportLevelToJson(LevelData level)
        {
            return JsonUtility.ToJson(level, true);
        }

        public LevelData ImportLevelFromJson(string json)
        {
            try
            {
                LevelData level = JsonUtility.FromJson<LevelData>(json);
                if (level != null)
                {
                    SaveLevel(level);
                    return level;
                }
            }
            catch (Exception e)
            {
                Debug.LogError($"导入关卡失败: {e.Message}");
            }
            return null;
        }

        public void ReloadLevels()
        {
            LoadAllLevels();
            OnLevelsChanged?.Invoke();
        }

        public int GetNextLevelId(int currentLevelId)
        {
            LevelData currentLevel = GetLevelById(currentLevelId);
            if (currentLevel == null) return -1;

            int currentIndex = _levelCollection.levels.IndexOf(currentLevel);
            if (currentIndex < 0 || currentIndex >= _levelCollection.GetLevelCount() - 1)
                return -1;

            return _levelCollection.levels[currentIndex + 1].levelId;
        }

        public bool HasNextLevel(int currentLevelId)
        {
            return GetNextLevelId(currentLevelId) > 0;
        }

        public void Cleanup()
        {
            _levelCache.Clear();
            _levelCollection = null;
            _isInitialized = false;
        }
    }
}
