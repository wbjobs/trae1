using System;
using System.Collections.Generic;
using UnityEngine;

namespace PhysicsPuzzleGame
{
    public class LevelGenerator : MonoBehaviour
    {
        private static LevelGenerator _instance;
        public static LevelGenerator Instance
        {
            get
            {
                if (_instance == null)
                {
                    GameObject go = new GameObject("LevelGenerator");
                    _instance = go.AddComponent<LevelGenerator>();
                    DontDestroyOnLoad(go);
                }
                return _instance;
            }
        }

        private GameObject _levelRoot;
        private List<GameObject> _generatedObjects = new List<GameObject>();
        private LevelData _currentLevelData;
        private System.Random _randomGenerator;
        private bool _isGenerated = false;

        public GameObject LevelRoot => _levelRoot;
        public LevelData CurrentLevelData => _currentLevelData;
        public bool IsGenerated => _isGenerated;

        public event Action<LevelData> OnLevelGenerated;
        public event Action OnLevelCleared;

        public void Initialize(int seed = -1)
        {
            _randomGenerator = seed >= 0 ? new System.Random(seed) : new System.Random();
        }

        public GameObject GenerateLevel(LevelData levelData)
        {
            ClearLevel();

            _currentLevelData = levelData;
            _levelRoot = new GameObject($"Level_{levelData.levelId}");
            _levelRoot.transform.SetParent(transform);

            GenerateLevelBounds(levelData);
            GenerateGoal(levelData.goal);
            GenerateObstacles(levelData.obstacles);
            GenerateItems(levelData.items);

            _isGenerated = true;
            OnLevelGenerated?.Invoke(levelData);

            return _levelRoot;
        }

        private void GenerateLevelBounds(LevelData levelData)
        {
            GameObject boundsObj = new GameObject("LevelBounds");
            boundsObj.transform.SetParent(_levelRoot.transform);
            boundsObj.transform.position = Vector3.zero;

            BoxCollider2D leftWall = CreateBoundary(
                "LeftWall",
                new Vector2(-levelData.levelBounds.x / 2f - 0.5f, 0),
                new Vector2(1f, levelData.levelBounds.y + 2f),
                boundsObj.transform);

            BoxCollider2D rightWall = CreateBoundary(
                "RightWall",
                new Vector2(levelData.levelBounds.x / 2f + 0.5f, 0),
                new Vector2(1f, levelData.levelBounds.y + 2f),
                boundsObj.transform);

            BoxCollider2D bottom = CreateBoundary(
                "Bottom",
                new Vector2(0, -levelData.levelBounds.y / 2f - 0.5f),
                new Vector2(levelData.levelBounds.x + 2f, 1f),
                boundsObj.transform);

            BoxCollider2D top = CreateBoundary(
                "Top",
                new Vector2(0, levelData.levelBounds.y / 2f + 0.5f),
                new Vector2(levelData.levelBounds.x + 2f, 1f),
                boundsObj.transform);

            _generatedObjects.Add(boundsObj);
        }

        private BoxCollider2D CreateBoundary(string name, Vector2 position, Vector2 size, Transform parent)
        {
            GameObject boundaryObj = new GameObject(name);
            boundaryObj.transform.SetParent(parent);
            boundaryObj.transform.position = position;

            BoxCollider2D collider = boundaryObj.AddComponent<BoxCollider2D>();
            collider.size = Vector2.one;

            Rigidbody2D rb = boundaryObj.AddComponent<Rigidbody2D>();
            rb.bodyType = RigidbodyType2D.Static;

            boundaryObj.layer = (int)PhysicsLayer.Platform;

            boundaryObj.transform.localScale = new Vector3(size.x, size.y, 1);

            _generatedObjects.Add(boundaryObj);
            return collider;
        }

        private void GenerateGoal(GoalData goalData)
        {
            GameObject goalObj = new GameObject("Goal");
            goalObj.transform.SetParent(_levelRoot.transform);
            goalObj.transform.position = goalData.position;

            BoxCollider2D collider = goalObj.AddComponent<BoxCollider2D>();
            collider.size = Vector2.one;
            collider.isTrigger = true;

            Goal goal = goalObj.AddComponent<Goal>();
            goal.Initialize(goalData);

            goalObj.layer = (int)PhysicsLayer.Goal;
            goalObj.transform.localScale = new Vector3(goalData.size.x, goalData.size.y, 1);

            _generatedObjects.Add(goalObj);
        }

        private void GenerateObstacles(List<ObstacleData> obstacleDataList)
        {
            foreach (var obstacleData in obstacleDataList)
            {
                GameObject obstacleObj = GenerateObstacle(obstacleData);
                if (obstacleObj != null)
                {
                    obstacleObj.transform.SetParent(_levelRoot.transform);
                    _generatedObjects.Add(obstacleObj);
                }
            }
        }

        private GameObject GenerateObstacle(ObstacleData data)
        {
            GameObject obstacleObj = new GameObject($"Obstacle_{data.obstacleType}");
            obstacleObj.transform.position = data.position;
            obstacleObj.transform.rotation = Quaternion.Euler(0, 0, data.rotation);

            BoxCollider2D collider = obstacleObj.AddComponent<BoxCollider2D>();
            collider.size = Vector2.one;

            PhysicsMaterial2D material = new PhysicsMaterial2D("ObstacleMaterial");

            switch (data.obstacleType)
            {
                case ObstacleType.StaticPlatform:
                    material.friction = 0.6f;
                    material.bounciness = 0f;
                    collider.sharedMaterial = material;
                    obstacleObj.layer = (int)PhysicsLayer.Platform;
                    Rigidbody2D staticRb = obstacleObj.AddComponent<Rigidbody2D>();
                    staticRb.bodyType = RigidbodyType2D.Static;
                    CreateVisualRepresentation(obstacleObj, data.size, new Color(0.6f, 0.6f, 0.6f));
                    break;

                case ObstacleType.MovingPlatform:
                    material.friction = 0.4f;
                    material.bounciness = 0.1f;
                    collider.sharedMaterial = material;
                    obstacleObj.layer = (int)PhysicsLayer.MovingPlatform;
                    Rigidbody2D movingRb = obstacleObj.AddComponent<Rigidbody2D>();
                    movingRb.bodyType = RigidbodyType2D.Kinematic;
                    MovingPlatform movingPlatform = obstacleObj.AddComponent<MovingPlatform>();
                    movingPlatform.Initialize(data);
                    CreateVisualRepresentation(obstacleObj, data.size, new Color(0.3f, 0.6f, 0.9f));
                    break;

                case ObstacleType.BouncingPad:
                    material.friction = 0.1f;
                    material.bounciness = 0.8f;
                    collider.sharedMaterial = material;
                    obstacleObj.layer = (int)PhysicsLayer.Platform;
                    Rigidbody2D bounceRb = obstacleObj.AddComponent<Rigidbody2D>();
                    bounceRb.bodyType = RigidbodyType2D.Static;
                    BouncingPad bouncingPad = obstacleObj.AddComponent<BouncingPad>();
                    bouncingPad.Initialize(data);
                    CreateVisualRepresentation(obstacleObj, data.size, new Color(0.9f, 0.6f, 0.3f));
                    break;

                case ObstacleType.SpikeTrap:
                    material.friction = 0f;
                    material.bounciness = 0f;
                    collider.sharedMaterial = material;
                    obstacleObj.layer = (int)PhysicsLayer.Hazard;
                    Rigidbody2D spikeRb = obstacleObj.AddComponent<Rigidbody2D>();
                    spikeRb.bodyType = RigidbodyType2D.Static;
                    SpikeTrap spikeTrap = obstacleObj.AddComponent<SpikeTrap>();
                    CreateVisualRepresentation(obstacleObj, data.size, new Color(0.9f, 0.2f, 0.2f));
                    break;

                case ObstacleType.GravityZone:
                    collider.isTrigger = true;
                    obstacleObj.layer = (int)PhysicsLayer.Obstacle;
                    GravityZone gravityZone = obstacleObj.AddComponent<GravityZone>();
                    CreateVisualRepresentation(obstacleObj, data.size, new Color(0.5f, 0.5f, 1f, 0.3f));
                    break;

                case ObstacleType.Portal:
                    collider.isTrigger = true;
                    obstacleObj.layer = (int)PhysicsLayer.Obstacle;
                    Portal portal = obstacleObj.AddComponent<Portal>();
                    CreateVisualRepresentation(obstacleObj, data.size, new Color(0.8f, 0.3f, 0.9f));
                    break;
            }

            return obstacleObj;
        }

        private void GenerateItems(List<ItemData> itemDataList)
        {
            foreach (var itemData in itemDataList)
            {
                if (!itemData.isCollected)
                {
                    GameObject itemObj = GenerateItem(itemData);
                    if (itemObj != null)
                    {
                        itemObj.transform.SetParent(_levelRoot.transform);
                        _generatedObjects.Add(itemObj);
                    }
                }
            }
        }

        private GameObject GenerateItem(ItemData data)
        {
            GameObject itemObj = new GameObject($"Item_{data.itemType}");
            itemObj.transform.position = data.position;

            CircleCollider2D collider = itemObj.AddComponent<CircleCollider2D>();
            collider.radius = 0.5f;
            collider.isTrigger = true;

            Rigidbody2D rb = itemObj.AddComponent<Rigidbody2D>();
            rb.bodyType = RigidbodyType2D.Kinematic;

            Item item = itemObj.AddComponent<Item>();
            item.Initialize(data);

            itemObj.layer = (int)PhysicsLayer.Item;

            Color itemColor = GetItemColor(data.itemType);
            CreateItemVisual(itemObj, data.itemType, itemColor);

            return itemObj;
        }

        private Color GetItemColor(ItemType itemType)
        {
            switch (itemType)
            {
                case ItemType.Coin:
                    return new Color(1f, 0.85f, 0f);
                case ItemType.Key:
                    return new Color(0.9f, 0.8f, 0.3f);
                case ItemType.Booster:
                    return new Color(0.3f, 0.9f, 0.4f);
                case ItemType.Shield:
                    return new Color(0.3f, 0.6f, 0.9f);
                case ItemType.TimeBonus:
                    return new Color(0.9f, 0.5f, 0.7f);
                default:
                    return Color.white;
            }
        }

        private void CreateVisualRepresentation(GameObject obj, Vector2 size, Color color)
        {
            SpriteRenderer renderer = obj.AddComponent<SpriteRenderer>();
            renderer.color = color;
            renderer.sortingOrder = 10;

            Texture2D tex = new Texture2D(1, 1);
            tex.SetPixel(0, 0, color);
            tex.Apply();
            renderer.sprite = Sprite.Create(tex, new Rect(0, 0, 1, 1), new Vector2(0.5f, 0.5f));
            obj.transform.localScale = new Vector3(size.x, size.y, 1);
        }

        private void CreateItemVisual(GameObject obj, ItemType itemType, Color color)
        {
            SpriteRenderer renderer = obj.AddComponent<SpriteRenderer>();
            renderer.color = color;
            renderer.sortingOrder = 20;

            Texture2D tex = new Texture2D(32, 32);
            for (int y = 0; y < 32; y++)
            {
                for (int x = 0; x < 32; x++)
                {
                    float dist = Vector2.Distance(new Vector2(x, y), new Vector2(16, 16));
                    if (dist < 14f)
                    {
                        tex.SetPixel(x, y, color);
                    }
                    else if (dist < 16f)
                    {
                        tex.SetPixel(x, y, color * 0.7f);
                    }
                    else
                    {
                        tex.SetPixel(x, y, Color.clear);
                    }
                }
            }
            tex.Apply();
            renderer.sprite = Sprite.Create(tex, new Rect(0, 0, 32, 32), new Vector2(0.5f, 0.5f));
            renderer.sprite.name = itemType.ToString();
        }

        public GameObject GenerateDynamicObstacle(ObstacleType type, Vector2 position, Vector2 size)
        {
            ObstacleData data = new ObstacleData
            {
                obstacleType = type,
                position = position,
                size = size,
                isDynamic = true
            };

            GameObject obj = GenerateObstacle(data);
            if (obj != null)
            {
                obj.transform.SetParent(_levelRoot != null ? _levelRoot.transform : transform);
                _generatedObjects.Add(obj);
            }
            return obj;
        }

        public GameObject GenerateRandomObstacle(Vector2 position, Vector2 size)
        {
            Array values = Enum.GetValues(typeof(ObstacleType));
            ObstacleType randomType = (ObstacleType)values.GetValue(_randomGenerator.Next(values.Length));

            ObstacleData data = new ObstacleData
            {
                obstacleType = randomType,
                position = position,
                size = size,
                rotation = _randomGenerator.Next(360),
                moveSpeed = (float)(_randomGenerator.NextDouble() * 2 + 0.5),
                moveRange = new Vector2((float)_randomGenerator.NextDouble() * 3, (float)_randomGenerator.NextDouble() * 3),
                bounceForce = (float)(_randomGenerator.NextDouble() * 10 + 5),
                isDynamic = true
            };

            GameObject obj = GenerateObstacle(data);
            if (obj != null)
            {
                obj.transform.SetParent(_levelRoot != null ? _levelRoot.transform : transform);
                _generatedObjects.Add(obj);
            }
            return obj;
        }

        public List<GameObject> GenerateObstacleCluster(Vector2 center, int count, float spread)
        {
            List<GameObject> cluster = new List<GameObject>();

            for (int i = 0; i < count; i++)
            {
                Vector2 offset = new Vector2(
                    (float)(_randomGenerator.NextDouble() * spread * 2 - spread),
                    (float)(_randomGenerator.NextDouble() * spread * 2 - spread));

                Vector2 size = new Vector2(
                    (float)(_randomGenerator.NextDouble() * 3 + 1),
                    (float)(_randomGenerator.NextDouble() * 1.5 + 0.5));

                GameObject obj = GenerateRandomObstacle(center + offset, size);
                if (obj != null)
                    cluster.Add(obj);
            }

            return cluster;
        }

        public void RemoveGeneratedObject(GameObject obj)
        {
            if (_generatedObjects.Contains(obj))
            {
                _generatedObjects.Remove(obj);
                Destroy(obj);
            }
        }

        public void ClearLevel()
        {
            if (_levelRoot != null)
            {
                Destroy(_levelRoot);
                _levelRoot = null;
            }

            foreach (var obj in _generatedObjects)
            {
                if (obj != null)
                    Destroy(obj);
            }
            _generatedObjects.Clear();

            _currentLevelData = null;
            _isGenerated = false;
        }

        public List<GameObject> GetAllGeneratedObjects()
        {
            return _generatedObjects;
        }

        public List<GameObject> GetObjectsByType<T>() where T : Component
        {
            List<GameObject> result = new List<GameObject>();
            foreach (var obj in _generatedObjects)
            {
                if (obj != null && obj.GetComponent<T>() != null)
                    result.Add(obj);
            }
            return result;
        }

        public void NotifyLevelCleared()
        {
            OnLevelCleared?.Invoke();
        }
    }

    public class Goal : MonoBehaviour
    {
        private GoalData _goalData;
        private bool _isReached = false;

        public GoalData GoalData => _goalData;
        public bool IsReached => _isReached;

        public event Action<Goal> OnGoalReached;

        public void Initialize(GoalData data)
        {
            _goalData = data;
        }

        private void OnTriggerEnter2D(Collider2D other)
        {
            if (!_isReached && other.CompareTag("Player"))
            {
                PlayerController player = other.GetComponent<PlayerController>();
                if (player != null)
                {
                    int collectedItems = LevelGenerator.Instance.CurrentLevelData.GetCollectedItemCount();
                    if (collectedItems >= _goalData.requiredItems)
                    {
                        _isReached = true;
                        OnGoalReached?.Invoke(this);
                        LevelGenerator.Instance.NotifyLevelCleared();
                    }
                }
            }
        }
    }

    public class MovingPlatform : MonoBehaviour
    {
        private ObstacleData _obstacleData;
        private Vector2 _startPosition;
        private Vector2 _targetPosition;
        private bool _isMovingForward = true;

        public void Initialize(ObstacleData data)
        {
            _obstacleData = data;
            _startPosition = transform.position;
            _targetPosition = _startPosition + data.moveRange;
        }

        private void FixedUpdate()
        {
            if (_obstacleData == null) return;

            Vector2 currentPos = transform.position;
            Vector2 targetPos = _isMovingForward ? _targetPosition : _startPosition;

            transform.position = Vector2.MoveTowards(currentPos, targetPos, _obstacleData.moveSpeed * Time.fixedDeltaTime);

            if (Vector2.Distance(currentPos, targetPos) < 0.01f)
            {
                _isMovingForward = !_isMovingForward;
            }
        }

        private void OnCollisionEnter2D(Collision2D collision)
        {
            if (collision.gameObject.CompareTag("Player"))
            {
                collision.transform.SetParent(transform);
            }
        }

        private void OnCollisionExit2D(Collision2D collision)
        {
            if (collision.gameObject.CompareTag("Player"))
            {
                collision.transform.SetParent(null);
            }
        }
    }

    public class BouncingPad : MonoBehaviour
    {
        private ObstacleData _obstacleData;
        private float _bounceForce = 10f;

        public float BounceForce => _bounceForce;

        public void Initialize(ObstacleData data)
        {
            _obstacleData = data;
            _bounceForce = data.bounceForce > 0 ? data.bounceForce : 10f;
        }

        private void OnCollisionEnter2D(Collision2D collision)
        {
            Rigidbody2D rb = collision.rigidbody;
            if (rb != null)
            {
                Vector2 bounceDirection = transform.up.normalized;
                rb.velocity = new Vector2(rb.velocity.x, 0);
                rb.AddForce(bounceDirection * _bounceForce, ForceMode2D.Impulse);
            }
        }
    }

    public class SpikeTrap : MonoBehaviour
    {
        public event Action<PlayerController> OnPlayerHit;

        private void OnTriggerEnter2D(Collider2D other)
        {
            if (other.CompareTag("Player"))
            {
                PlayerController player = other.GetComponent<PlayerController>();
                if (player != null)
                {
                    player.TakeDamage();
                    OnPlayerHit?.Invoke(player);
                }
            }
        }

        private void OnCollisionEnter2D(Collision2D collision)
        {
            if (collision.gameObject.CompareTag("Player"))
            {
                PlayerController player = collision.gameObject.GetComponent<PlayerController>();
                if (player != null)
                {
                    player.TakeDamage();
                    OnPlayerHit?.Invoke(player);
                }
            }
        }
    }

    public class Portal : MonoBehaviour
    {
        private Vector2 _destination;
        private bool _isActive = true;

        public Vector2 Destination => _destination;
        public bool IsActive => _isActive;

        public void SetDestination(Vector2 destination)
        {
            _destination = destination;
        }

        public void Activate()
        {
            _isActive = true;
        }

        public void Deactivate()
        {
            _isActive = false;
        }

        private void OnTriggerEnter2D(Collider2D other)
        {
            if (_isActive && other.CompareTag("Player"))
            {
                PlayerController player = other.GetComponent<PlayerController>();
                if (player != null)
                {
                    player.TeleportTo(_destination);
                }
            }
        }
    }
}
