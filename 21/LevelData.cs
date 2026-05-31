using System;
using System.Collections.Generic;
using UnityEngine;

namespace PhysicsPuzzleGame
{
    [Serializable]
    public enum ObstacleType
    {
        StaticPlatform,
        MovingPlatform,
        BouncingPad,
        SpikeTrap,
        GravityZone,
        Portal
    }

    [Serializable]
    public enum ItemType
    {
        Coin,
        Key,
        Booster,
        Shield,
        TimeBonus
    }

    [Serializable]
    public enum GravityDirection
    {
        Down,
        Up,
        Left,
        Right
    }

    [Serializable]
    public class GravityZoneData
    {
        public Vector2 position;
        public Vector2 size;
        public float gravityScale = 1f;
        public GravityDirection gravityDirection = GravityDirection.Down;
    }

    [Serializable]
    public class ObstacleData
    {
        public ObstacleType obstacleType;
        public Vector2 position;
        public Vector2 size;
        public float rotation;
        public float moveSpeed;
        public Vector2 moveRange;
        public float bounceForce;
        public bool isDynamic;
    }

    [Serializable]
    public class ItemData
    {
        public ItemType itemType;
        public Vector2 position;
        public int value;
        public bool isCollected;
    }

    [Serializable]
    public class PlayerStartData
    {
        public Vector2 position;
        public float initialVelocity;
    }

    [Serializable]
    public class GoalData
    {
        public Vector2 position;
        public Vector2 size;
        public int requiredItems;
    }

    [Serializable]
    public class LevelData
    {
        public int levelId;
        public string levelName;
        public string description;
        public Vector2 levelBounds;
        public float globalGravityScale = 1f;
        public GravityDirection globalGravityDirection = GravityDirection.Down;
        public PlayerStartData playerStart;
        public GoalData goal;
        public List<ObstacleData> obstacles = new List<ObstacleData>();
        public List<ItemData> items = new List<ItemData>();
        public List<GravityZoneData> gravityZones = new List<GravityZoneData>();
        public float timeLimit;
        public int parTime;
        public int difficulty;

        public LevelData() { }

        public LevelData(int id, string name)
        {
            levelId = id;
            levelName = name;
        }

        public Vector2 GetGravityVector()
        {
            return GetGravityVectorFromDirection(globalGravityDirection) * globalGravityScale;
        }

        public static Vector2 GetGravityVectorFromDirection(GravityDirection direction)
        {
            switch (direction)
            {
                case GravityDirection.Down:
                    return new Vector2(0, -9.81f);
                case GravityDirection.Up:
                    return new Vector2(0, 9.81f);
                case GravityDirection.Left:
                    return new Vector2(-9.81f, 0);
                case GravityDirection.Right:
                    return new Vector2(9.81f, 0);
                default:
                    return new Vector2(0, -9.81f);
            }
        }

        public bool IsPositionInLevelBounds(Vector2 position)
        {
            return position.x >= -levelBounds.x / 2 &&
                   position.x <= levelBounds.x / 2 &&
                   position.y >= -levelBounds.y / 2 &&
                   position.y <= levelBounds.y / 2;
        }

        public int GetTotalItemCount()
        {
            return items.Count;
        }

        public int GetCollectedItemCount()
        {
            int count = 0;
            foreach (var item in items)
            {
                if (item.isCollected) count++;
            }
            return count;
        }

        public bool AreAllItemsCollected()
        {
            foreach (var item in items)
            {
                if (!item.isCollected) return false;
            }
            return true;
        }

        public void ResetItemStates()
        {
            foreach (var item in items)
            {
                item.isCollected = false;
            }
        }
    }

    [Serializable]
    public class LevelCollection
    {
        public List<LevelData> levels = new List<LevelData>();

        public LevelData GetLevelById(int id)
        {
            foreach (var level in levels)
            {
                if (level.levelId == id)
                    return level;
            }
            return null;
        }

        public LevelData GetLevelByName(string name)
        {
            foreach (var level in levels)
            {
                if (level.levelName == name)
                    return level;
            }
            return null;
        }

        public int GetLevelCount()
        {
            return levels.Count;
        }
    }
}
