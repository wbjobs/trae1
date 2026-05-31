using System;
using System.Collections.Generic;
using UnityEngine;

namespace PhysicsPuzzleGame
{
    public enum DifficultyLevel
    {
        Easy = 1,
        Normal = 2,
        Hard = 3,
        Expert = 4,
        Master = 5
    }

    public enum PlayerSkillLevel
    {
        Beginner,
        Casual,
        Intermediate,
        Advanced,
        Expert
    }

    public class DifficultyAdapter : MonoBehaviour
    {
        private static DifficultyAdapter _instance;
        public static DifficultyAdapter Instance
        {
            get
            {
                if (_instance == null)
                {
                    GameObject go = new GameObject("DifficultyAdapter");
                    _instance = go.AddComponent<DifficultyAdapter>();
                    DontDestroyOnLoad(go);
                }
                return _instance;
            }
        }

        private DifficultyLevel _currentDifficulty = DifficultyLevel.Normal;
        private PlayerSkillLevel _playerSkill = PlayerSkillLevel.Casual;

        private int _totalDeaths;
        private int _currentLevelDeaths;
        private int _consecutiveCompletions;
        private float _averageCompletionTime;
        private int _totalAttempts;
        private int _successfulAttempts;

        private float _skillScore;
        private const float SKILL_UPDATE_INTERVAL = 5f;
        private float _lastSkillUpdateTime;

        private Dictionary<DifficultyLevel, DifficultyConfig> _difficultyConfigs
            = new Dictionary<DifficultyLevel, DifficultyConfig>();

        public DifficultyLevel CurrentDifficulty => _currentDifficulty;
        public PlayerSkillLevel PlayerSkill => _playerSkill;
        public float SkillScore => _skillScore;

        public event Action<DifficultyLevel> OnDifficultyChanged;
        public event Action<PlayerSkillLevel> OnPlayerSkillChanged;

        [Serializable]
        public class DifficultyConfig
        {
            public DifficultyLevel Level;
            public float GravityMultiplier;
            public float JumpForceMultiplier;
            public float MoveSpeedMultiplier;
            public float EnemySpeedMultiplier;
            public int MaxHealth;
            public float TimeLimitMultiplier;
            public int RequiredItemsMultiplier;
            public float ScoreMultiplier;
            public bool EnableHints;
            public bool EnableCheckpoints;
            public bool EnableInvincibilityFrames;
        }

        private void Start()
        {
            InitializeDefaultConfigs();
        }

        private void InitializeDefaultConfigs()
        {
            _difficultyConfigs[DifficultyLevel.Easy] = new DifficultyConfig
            {
                Level = DifficultyLevel.Easy,
                GravityMultiplier = 0.8f,
                JumpForceMultiplier = 1.2f,
                MoveSpeedMultiplier = 1.1f,
                EnemySpeedMultiplier = 0.7f,
                MaxHealth = 5,
                TimeLimitMultiplier = 1.5f,
                RequiredItemsMultiplier = 1,
                ScoreMultiplier = 0.5f,
                EnableHints = true,
                EnableCheckpoints = true,
                EnableInvincibilityFrames = true
            };

            _difficultyConfigs[DifficultyLevel.Normal] = new DifficultyConfig
            {
                Level = DifficultyLevel.Normal,
                GravityMultiplier = 1f,
                JumpForceMultiplier = 1f,
                MoveSpeedMultiplier = 1f,
                EnemySpeedMultiplier = 1f,
                MaxHealth = 3,
                TimeLimitMultiplier = 1f,
                RequiredItemsMultiplier = 1,
                ScoreMultiplier = 1f,
                EnableHints = false,
                EnableCheckpoints = true,
                EnableInvincibilityFrames = true
            };

            _difficultyConfigs[DifficultyLevel.Hard] = new DifficultyConfig
            {
                Level = DifficultyLevel.Hard,
                GravityMultiplier = 1.2f,
                JumpForceMultiplier = 0.9f,
                MoveSpeedMultiplier = 0.95f,
                EnemySpeedMultiplier = 1.3f,
                MaxHealth = 2,
                TimeLimitMultiplier = 0.8f,
                RequiredItemsMultiplier = 2,
                ScoreMultiplier = 1.5f,
                EnableHints = false,
                EnableCheckpoints = false,
                EnableInvincibilityFrames = true
            };

            _difficultyConfigs[DifficultyLevel.Expert] = new DifficultyConfig
            {
                Level = DifficultyLevel.Expert,
                GravityMultiplier = 1.4f,
                JumpForceMultiplier = 0.85f,
                MoveSpeedMultiplier = 0.9f,
                EnemySpeedMultiplier = 1.5f,
                MaxHealth = 1,
                TimeLimitMultiplier = 0.6f,
                RequiredItemsMultiplier = 2,
                ScoreMultiplier = 2f,
                EnableHints = false,
                EnableCheckpoints = false,
                EnableInvincibilityFrames = false
            };

            _difficultyConfigs[DifficultyLevel.Master] = new DifficultyConfig
            {
                Level = DifficultyLevel.Master,
                GravityMultiplier = 1.6f,
                JumpForceMultiplier = 0.8f,
                MoveSpeedMultiplier = 0.85f,
                EnemySpeedMultiplier = 2f,
                MaxHealth = 1,
                TimeLimitMultiplier = 0.4f,
                RequiredItemsMultiplier = 3,
                ScoreMultiplier = 3f,
                EnableHints = false,
                EnableCheckpoints = false,
                EnableInvincibilityFrames = false
            };
        }

        public void Initialize()
        {
            _totalDeaths = 0;
            _currentLevelDeaths = 0;
            _consecutiveCompletions = 0;
            _averageCompletionTime = 0;
            _totalAttempts = 0;
            _successfulAttempts = 0;
            _skillScore = 0.5f;

            UpdatePlayerSkill();
        }

        public DifficultyConfig GetCurrentConfig()
        {
            return _difficultyConfigs[_currentDifficulty];
        }

        public DifficultyConfig GetConfig(DifficultyLevel level)
        {
            return _difficultyConfigs.ContainsKey(level) ? _difficultyConfigs[level] : _difficultyConfigs[DifficultyLevel.Normal];
        }

        public void SetDifficulty(DifficultyLevel difficulty)
        {
            if (_currentDifficulty != difficulty)
            {
                _currentDifficulty = difficulty;
                OnDifficultyChanged?.Invoke(_currentDifficulty);
                ApplyDifficultySettings();
            }
        }

        private void ApplyDifficultySettings()
        {
            DifficultyConfig config = GetCurrentConfig();

            if (PlayerController.Instance != null)
            {
                PlayerController.Instance.maxHealth = config.MaxHealth;
            }

            if (GravitySimulator.Instance != null)
            {
                GravitySimulator.Instance.SetGlobalGravityScale(
                    GravitySimulator.Instance.GlobalGravityScale * config.GravityMultiplier);
            }
        }

        public void RecordDeath()
        {
            _totalDeaths++;
            _currentLevelDeaths++;
            _consecutiveCompletions = 0;
            _skillScore = Mathf.Max(0, _skillScore - 0.05f);
        }

        public void RecordCompletion(float completionTime, int levelParTime)
        {
            _consecutiveCompletions++;
            _successfulAttempts++;
            _totalAttempts++;

            float timeRatio = completionTime / Mathf.Max(1, levelParTime);
            float timeScore = Mathf.Clamp01(1f - timeRatio * 0.5f);
            _skillScore = Mathf.Lerp(_skillScore, timeScore, 0.1f);

            _averageCompletionTime = (_averageCompletionTime * (_totalAttempts - 1) + completionTime) / _totalAttempts;
        }

        public void RecordAttempt()
        {
            _totalAttempts++;
        }

        public void ResetLevelStats()
        {
            _currentLevelDeaths = 0;
        }

        private void Update()
        {
            if (Time.time - _lastSkillUpdateTime >= SKILL_UPDATE_INTERVAL)
            {
                _lastSkillUpdateTime = Time.time;
                UpdatePlayerSkill();
                AutoAdjustDifficulty();
            }
        }

        private void UpdatePlayerSkill()
        {
            PlayerSkillLevel newSkill;

            if (_skillScore < 0.2f)
                newSkill = PlayerSkillLevel.Beginner;
            else if (_skillScore < 0.4f)
                newSkill = PlayerSkillLevel.Casual;
            else if (_skillScore < 0.6f)
                newSkill = PlayerSkillLevel.Intermediate;
            else if (_skillScore < 0.8f)
                newSkill = PlayerSkillLevel.Advanced;
            else
                newSkill = PlayerSkillLevel.Expert;

            if (newSkill != _playerSkill)
            {
                _playerSkill = newSkill;
                OnPlayerSkillChanged?.Invoke(_playerSkill);
            }
        }

        private void AutoAdjustDifficulty()
        {
            if (_consecutiveCompletions >= 3 && _skillScore > 0.7f)
            {
                if (_currentDifficulty < DifficultyLevel.Master)
                {
                    SetDifficulty(_currentDifficulty + 1);
                }
            }
            else if (_currentLevelDeaths >= 5 && _skillScore < 0.3f)
            {
                if (_currentDifficulty > DifficultyLevel.Easy)
                {
                    SetDifficulty(_currentDifficulty - 1);
                }
            }
        }

        public void AdaptLevelData(LevelData levelData)
        {
            if (levelData == null) return;

            DifficultyConfig config = GetCurrentConfig();

            levelData.globalGravityScale *= config.GravityMultiplier;
            levelData.timeLimit *= config.TimeLimitMultiplier;

            if (config.RequiredItemsMultiplier > 1)
            {
                levelData.goal.requiredItems *= config.RequiredItemsMultiplier;
            }

            foreach (var obstacle in levelData.obstacles)
            {
                if (obstacle.obstacleType == ObstacleType.MovingPlatform)
                {
                    obstacle.moveSpeed *= config.EnemySpeedMultiplier;
                }
                else if (obstacle.obstacleType == ObstacleType.BouncingPad)
                {
                    obstacle.bounceForce *= config.JumpForceMultiplier;
                }
            }
        }

        public int CalculateAdaptedScore(int baseScore)
        {
            return Mathf.RoundToInt(baseScore * GetCurrentConfig().ScoreMultiplier);
        }

        public float GetCompletionTimeLimit(float baseTimeLimit)
        {
            return baseTimeLimit * GetCurrentConfig().TimeLimitMultiplier;
        }

        public List<string> GetCurrentTips()
        {
            List<string> tips = new List<string>();
            DifficultyConfig config = GetCurrentConfig();

            if (config.EnableHints)
            {
                switch (_playerSkill)
                {
                    case PlayerSkillLevel.Beginner:
                        tips.Add("按住空格键跳跃，长按跳得更高");
                        tips.Add("注意收集金币以获得更高分数");
                        tips.Add("碰到尖刺会受伤，小心躲避");
                        tips.Add("移动平台会改变位置，注意时机");
                        break;
                    case PlayerSkillLevel.Casual:
                        tips.Add("尝试在空中再次跳跃进行二段跳");
                        tips.Add("利用弹跳板可以跳得更高");
                        tips.Add("蓝色区域是重力区，会改变重力方向");
                        break;
                    case PlayerSkillLevel.Intermediate:
                        tips.Add("尝试连跳穿越复杂地形");
                        tips.Add("收集所有道具可以解锁隐藏内容");
                        break;
                    case PlayerSkillLevel.Advanced:
                        tips.Add("尝试用最少的时间完成关卡");
                        tips.Add("挑战不受伤完成关卡以获得额外奖励");
                        break;
                    case PlayerSkillLevel.Expert:
                        tips.Add("你是真正的大师！挑战极限吧！");
                        break;
                }
            }

            return tips;
        }

        public void SetDifficultyConfig(DifficultyLevel level, DifficultyConfig config)
        {
            _difficultyConfigs[level] = config;
        }

        public PlayerSkillLevel CalculateSkillLevelFromStats(int totalDeaths, float averageTime, float parTime)
        {
            float score = 0.5f;

            if (averageTime > 0 && parTime > 0)
            {
                float timeRatio = averageTime / parTime;
                if (timeRatio < 0.5f) score += 0.3f;
                else if (timeRatio < 0.8f) score += 0.2f;
                else if (timeRatio < 1.2f) score += 0.1f;
                else if (timeRatio > 2f) score -= 0.2f;
            }

            if (totalDeaths < 3) score += 0.2f;
            else if (totalDeaths > 10) score -= 0.2f;

            score = Mathf.Clamp01(score);

            if (score < 0.2f) return PlayerSkillLevel.Beginner;
            else if (score < 0.4f) return PlayerSkillLevel.Casual;
            else if (score < 0.6f) return PlayerSkillLevel.Intermediate;
            else if (score < 0.8f) return PlayerSkillLevel.Advanced;
            else return PlayerSkillLevel.Expert;
        }

        public string GetDifficultyDescription(DifficultyLevel level)
        {
            switch (level)
            {
                case DifficultyLevel.Easy:
                    return "简单 - 适合新手玩家，更多生命值和提示";
                case DifficultyLevel.Normal:
                    return "普通 - 标准难度，平衡的挑战体验";
                case DifficultyLevel.Hard:
                    return "困难 - 更高的重力和更少的生命值";
                case DifficultyLevel.Expert:
                    return "专家 - 极限挑战，只有一条命";
                case DifficultyLevel.Master:
                    return "大师 - 终极挑战，时间紧迫";
                default:
                    return "";
            }
        }

        public void Cleanup()
        {
            _difficultyConfigs.Clear();
        }
    }
}
