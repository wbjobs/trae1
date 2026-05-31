using System;
using System.Collections.Generic;
using UnityEngine;

namespace PhysicsPuzzleGame
{
    public class LevelValidator : MonoBehaviour
    {
        private static LevelValidator _instance;
        public static LevelValidator Instance
        {
            get
            {
                if (_instance == null)
                {
                    GameObject go = new GameObject("LevelValidator");
                    _instance = go.AddComponent<LevelValidator>();
                    DontDestroyOnLoad(go);
                }
                return _instance;
            }
        }

        public class ValidationResult
        {
            public bool IsValid;
            public List<string> Errors = new List<string>();
            public List<string> Warnings = new List<string>();
            public float ReachabilityScore;
            public bool IsGoalReachable;
            public bool AreAllItemsReachable;
            public float EstimatedCompletionTime;
            public int DifficultyRating;
        }

        private LevelData _currentLevelData;
        private bool _isValidationRunning = false;
        private List<Vector2> _exploredPositions = new List<Vector2>();

        public bool IsValidationRunning => _isValidationRunning;

        public event Action<ValidationResult> OnValidationComplete;

        public ValidationResult ValidateLevel(LevelData levelData)
        {
            _currentLevelData = levelData;
            ValidationResult result = new ValidationResult();

            ValidateBasicStructure(levelData, result);
            ValidatePlayerStart(levelData, result);
            ValidateGoal(levelData, result);
            ValidateObstacles(levelData, result);
            ValidateItems(levelData, result);
            ValidateGravityZones(levelData, result);

            if (result.Errors.Count == 0)
            {
                ValidateReachability(levelData, result);
                CalculateDifficulty(levelData, result);
            }

            result.IsValid = result.Errors.Count == 0;
            OnValidationComplete?.Invoke(result);

            return result;
        }

        private void ValidateBasicStructure(LevelData levelData, ValidationResult result)
        {
            if (levelData.levelBounds.x <= 0 || levelData.levelBounds.y <= 0)
            {
                result.Errors.Add("关卡边界必须大于零");
            }

            if (levelData.levelBounds.x > 100f || levelData.levelBounds.y > 100f)
            {
                result.Warnings.Add("关卡边界可能过大");
            }

            if (levelData.timeLimit < 0)
            {
                result.Errors.Add("时间限制不能为负数");
            }

            if (levelData.parTime < 0)
            {
                result.Errors.Add("标准完成时间不能为负数");
            }

            if (levelData.difficulty < 1 || levelData.difficulty > 10)
            {
                result.Errors.Add("难度等级必须在1-10之间");
            }
        }

        private void ValidatePlayerStart(LevelData levelData, ValidationResult result)
        {
            if (levelData.playerStart == null)
            {
                result.Errors.Add("缺少玩家起始位置");
                return;
            }

            if (!levelData.IsPositionInLevelBounds(levelData.playerStart.position))
            {
                result.Errors.Add("玩家起始位置不在关卡边界内");
            }

            if (levelData.playerStart.initialVelocity < 0)
            {
                result.Errors.Add("玩家初始速度不能为负数");
            }
        }

        private void ValidateGoal(LevelData levelData, ValidationResult result)
        {
            if (levelData.goal == null)
            {
                result.Errors.Add("缺少目标位置");
                return;
            }

            if (!levelData.IsPositionInLevelBounds(levelData.goal.position))
            {
                result.Errors.Add("目标位置不在关卡边界内");
            }

            if (levelData.goal.size.x <= 0 || levelData.goal.size.y <= 0)
            {
                result.Errors.Add("目标尺寸必须大于零");
            }

            if (levelData.goal.requiredItems < 0)
            {
                result.Errors.Add("所需道具数量不能为负数");
            }

            if (levelData.goal.requiredItems > levelData.GetTotalItemCount())
            {
                result.Errors.Add("所需道具数量超过关卡道具总数");
            }
        }

        private void ValidateObstacles(LevelData levelData, ValidationResult result)
        {
            if (levelData.obstacles == null)
            {
                result.Errors.Add("障碍物列表为空");
                return;
            }

            HashSet<Vector2Int> usedPositions = new HashSet<Vector2Int>();

            foreach (var obstacle in levelData.obstacles)
            {
                if (obstacle.size.x <= 0 || obstacle.size.y <= 0)
                {
                    result.Errors.Add($"障碍物 {obstacle.obstacleType} 尺寸必须大于零");
                }

                if (!levelData.IsPositionInLevelBounds(obstacle.position))
                {
                    result.Warnings.Add($"障碍物 {obstacle.obstacleType} 位置在关卡边界外");
                }

                Vector2Int gridPos = new Vector2Int(
                    Mathf.RoundToInt(obstacle.position.x),
                    Mathf.RoundToInt(obstacle.position.y));

                if (usedPositions.Contains(gridPos))
                {
                    result.Warnings.Add($"多个障碍物可能重叠在位置 {obstacle.position}");
                }
                usedPositions.Add(gridPos);

                if (obstacle.moveSpeed < 0)
                {
                    result.Errors.Add($"移动平台速度不能为负数");
                }

                if (obstacle.bounceForce < 0)
                {
                    result.Errors.Add($"弹跳板弹力不能为负数");
                }
            }
        }

        private void ValidateItems(LevelData levelData, ValidationResult result)
        {
            if (levelData.items == null)
            {
                result.Warnings.Add("关卡没有可收集的道具");
                return;
            }

            foreach (var item in levelData.items)
            {
                if (!levelData.IsPositionInLevelBounds(item.position))
                {
                    result.Warnings.Add($"道具 {item.itemType} 位置在关卡边界外");
                }

                if (item.value < 0)
                {
                    result.Errors.Add($"道具 {item.itemType} 数值不能为负数");
                }
            }
        }

        private void ValidateGravityZones(LevelData levelData, ValidationResult result)
        {
            if (levelData.gravityZones == null) return;

            foreach (var zone in levelData.gravityZones)
            {
                if (zone.size.x <= 0 || zone.size.y <= 0)
                {
                    result.Errors.Add("重力区域尺寸必须大于零");
                }

                if (!levelData.IsPositionInLevelBounds(zone.position))
                {
                    result.Warnings.Add("重力区域位置在关卡边界外");
                }

                if (zone.gravityScale < 0)
                {
                    result.Errors.Add("重力缩放不能为负数");
                }

                if (zone.gravityScale > 10f)
                {
                    result.Warnings.Add("重力缩放可能过大");
                }
            }
        }

        private void ValidateReachability(LevelData levelData, ValidationResult result)
        {
            _exploredPositions.Clear();

            bool goalReachable = IsPositionReachable(
                levelData.playerStart.position,
                levelData.goal.position,
                levelData);

            result.IsGoalReachable = goalReachable;
            if (!goalReachable)
            {
                result.Errors.Add("目标位置不可达");
            }

            bool allItemsReachable = true;
            int unreachableItems = 0;

            foreach (var item in levelData.items)
            {
                if (!item.isCollected)
                {
                    bool itemReachable = IsPositionReachable(
                        levelData.playerStart.position,
                        item.position,
                        levelData);

                    if (!itemReachable)
                    {
                        allItemsReachable = false;
                        unreachableItems++;
                    }
                }
            }

            result.AreAllItemsReachable = allItemsReachable;
            if (!allItemsReachable)
            {
                result.Warnings.Add($"{unreachableItems}个道具位置不可达");
            }

            float reachabilityScore = 1f;
            if (!goalReachable) reachabilityScore -= 0.5f;
            if (!allItemsReachable) reachabilityScore -= 0.3f;
            result.ReachabilityScore = Mathf.Max(0f, reachabilityScore);

            float estimatedTime = CalculateEstimatedTime(levelData);
            result.EstimatedCompletionTime = estimatedTime;
        }

        private bool IsPositionReachable(Vector2 start, Vector2 target, LevelData levelData)
        {
            float distance = Vector2.Distance(start, target);
            if (distance > 50f) return false;

            int steps = Mathf.CeilToInt(distance / 0.5f);
            Vector2 currentPos = start;
            LayerMask obstacleMask = LayerMask.GetMask("Platform", "MovingPlatform", "Hazard");

            for (int i = 0; i < steps; i++)
            {
                float t = (float)(i + 1) / steps;
                Vector2 nextPos = Vector2.Lerp(start, target, t);

                bool hasCollision = Physics2D.OverlapBox(
                    nextPos,
                    new Vector2(0.8f, 0.8f),
                    0f,
                    obstacleMask);

                if (hasCollision)
                {
                    bool pathFound = TryFindPathAroundObstacle(currentPos, target, levelData, obstacleMask);
                    if (!pathFound) return false;
                }

                _exploredPositions.Add(nextPos);
                currentPos = nextPos;
            }

            return true;
        }

        private bool TryFindPathAroundObstacle(Vector2 from, Vector2 to, LevelData levelData, LayerMask obstacleMask)
        {
            Vector2[] directions = new Vector2[]
            {
                Vector2.up,
                Vector2.down,
                Vector2.left,
                Vector2.right,
                new Vector2(1, 1).normalized,
                new Vector2(1, -1).normalized,
                new Vector2(-1, 1).normalized,
                new Vector2(-1, -1).normalized
            };

            foreach (var dir in directions)
            {
                Vector2 testPos = from + dir * 2f;

                if (!levelData.IsPositionInLevelBounds(testPos)) continue;

                bool hasCollision = Physics2D.OverlapBox(
                    testPos,
                    new Vector2(0.8f, 0.8f),
                    0f,
                    obstacleMask);

                if (!hasCollision)
                {
                    return IsPositionReachable(testPos, to, levelData);
                }
            }

            return false;
        }

        private float CalculateEstimatedTime(LevelData levelData)
        {
            float baseTime = 30f;
            float distance = Vector2.Distance(levelData.playerStart.position, levelData.goal.position);
            baseTime += distance * 2f;
            baseTime += levelData.obstacles.Count * 3f;
            baseTime += levelData.items.Count * 2f;
            baseTime += levelData.gravityZones.Count * 5f;
            baseTime *= (1f + (levelData.difficulty - 1) * 0.15f);

            return Mathf.Max(10f, baseTime);
        }

        private void CalculateDifficulty(LevelData levelData, ValidationResult result)
        {
            int score = 0;
            score += levelData.obstacles.Count;
            score += levelData.gravityZones.Count * 2;
            score += Mathf.RoundToInt(levelData.globalGravityScale * 2);
            score += (int)levelData.globalGravityDirection;
            score += levelData.GetTotalItemCount() / 2;

            int calculatedDifficulty = Mathf.Clamp(1 + score / 5, 1, 10);
            result.DifficultyRating = calculatedDifficulty;

            if (Mathf.Abs(calculatedDifficulty - levelData.difficulty) > 3)
            {
                result.Warnings.Add($"设置难度({levelData.difficulty})与计算难度({calculatedDifficulty})差异较大");
            }
        }

        public ValidationResult QuickValidate(LevelData levelData)
        {
            ValidationResult result = new ValidationResult();

            if (levelData.playerStart == null)
                result.Errors.Add("缺少玩家起始位置");

            if (levelData.goal == null)
                result.Errors.Add("缺少目标位置");

            if (levelData.levelBounds.x <= 0 || levelData.levelBounds.y <= 0)
                result.Errors.Add("关卡边界无效");

            result.IsValid = result.Errors.Count == 0;
            return result;
        }

        public bool CheckWinCondition(PlayerController player, LevelData levelData)
        {
            if (player == null || levelData == null) return false;

            if (!levelData.AreAllItemsCollected() && levelData.goal.requiredItems > 0)
            {
                int collectedItems = levelData.GetCollectedItemCount();
                if (collectedItems < levelData.goal.requiredItems)
                    return false;
            }

            float distanceToGoal = Vector2.Distance(
                player.transform.position,
                levelData.goal.position);

            return distanceToGoal < Mathf.Max(levelData.goal.size.x, levelData.goal.size.y);
        }

        public bool CheckLoseCondition(PlayerController player, LevelData levelData)
        {
            if (player == null || levelData == null) return true;

            if (player.Health <= 0)
                return true;

            if (!levelData.IsPositionInLevelBounds(player.transform.position))
                return true;

            return false;
        }

        public void Cleanup()
        {
            _exploredPositions.Clear();
            _currentLevelData = null;
            _isValidationRunning = false;
        }
    }
}
