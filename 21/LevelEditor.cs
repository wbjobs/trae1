using System;
using System.Collections.Generic;
using UnityEngine;

namespace PhysicsPuzzleGame
{
    public enum EditorMode
    {
        None,
        PlaceObstacle,
        PlaceItem,
        PlaceGravityZone,
        SetStartPosition,
        SetGoalPosition,
        Erase,
        Move
    }

    public class LevelEditor : MonoBehaviour
    {
        private static LevelEditor _instance;
        public static LevelEditor Instance
        {
            get
            {
                if (_instance == null)
                {
                    GameObject go = new GameObject("LevelEditor");
                    _instance = go.AddComponent<LevelEditor>();
                    DontDestroyOnLoad(go);
                }
                return _instance;
            }
        }

        private LevelData _editingLevel;
        private GameObject _editorPreview;
        private EditorMode _currentMode = EditorMode.None;
        private ObstacleType _selectedObstacleType = ObstacleType.StaticPlatform;
        private ItemType _selectedItemType = ItemType.Coin;
        private GravityDirection _selectedGravityDirection = GravityDirection.Down;

        private Vector2 _currentMousePosition;
        private Vector2 _startDragPosition;
        private bool _isDragging;
        private GameObject _previewObject;

        private float _gridSize = 0.5f;
        private bool _snapToGrid = true;
        private Vector2 _currentObjectSize = new Vector2(2f, 0.5f);
        private float _currentGravityScale = 1f;

        private List<GameObject> _editorObjects = new List<GameObject>();

        public LevelData EditingLevel => _editingLevel;
        public EditorMode CurrentMode => _currentMode;
        public bool IsEditing => _editingLevel != null;

        public event Action<LevelData> OnLevelModified;
        public event Action<EditorMode> OnEditorModeChanged;
        public event Action<LevelData> OnLevelSaved;

        public void StartEditing(LevelData levelData)
        {
            _editingLevel = levelData;
            CreateEditorPreview();
            SetMode(EditorMode.PlaceObstacle);
        }

        public void StopEditing()
        {
            ClearEditorPreview();
            _editingLevel = null;
            _currentMode = EditorMode.None;
            OnEditorModeChanged?.Invoke(_currentMode);
        }

        private void CreateEditorPreview()
        {
            ClearEditorPreview();

            _editorPreview = new GameObject("EditorPreview");
            _editorPreview.transform.SetParent(transform);

            GameObject boundsObj = new GameObject("LevelBounds");
            boundsObj.transform.SetParent(_editorPreview.transform);

            LineRenderer boundsRenderer = boundsObj.AddComponent<LineRenderer>();
            boundsRenderer.material = new Material(Shader.Find("Sprites/Default"));
            boundsRenderer.startColor = new Color(0f, 1f, 0f, 0.5f);
            boundsRenderer.endColor = new Color(0f, 1f, 0f, 0.5f);
            boundsRenderer.startWidth = 0.1f;
            boundsRenderer.endWidth = 0.1f;
            boundsRenderer.positionCount = 5;
            boundsRenderer.loop = true;

            Vector2 bounds = _editingLevel.levelBounds;
            boundsRenderer.SetPosition(0, new Vector3(-bounds.x / 2f, -bounds.y / 2f, 0));
            boundsRenderer.SetPosition(1, new Vector3(bounds.x / 2f, -bounds.y / 2f, 0));
            boundsRenderer.SetPosition(2, new Vector3(bounds.x / 2f, bounds.y / 2f, 0));
            boundsRenderer.SetPosition(3, new Vector3(-bounds.x / 2f, bounds.y / 2f, 0));
            boundsRenderer.SetPosition(4, new Vector3(-bounds.x / 2f, -bounds.y / 2f, 0));

            RefreshEditorObjects();
        }

        private void RefreshEditorObjects()
        {
            ClearEditorObjects();

            foreach (var obstacle in _editingLevel.obstacles)
            {
                CreateEditorObstacle(obstacle);
            }

            foreach (var item in _editingLevel.items)
            {
                if (!item.isCollected)
                {
                    CreateEditorItem(item);
                }
            }

            foreach (var zone in _editingLevel.gravityZones)
            {
                CreateEditorGravityZone(zone);
            }

            CreateEditorStartPosition();
            CreateEditorGoal();
        }

        private void CreateEditorObstacle(ObstacleData data)
        {
            GameObject obj = new GameObject($"Editor_Obstacle_{data.obstacleType}");
            obj.transform.SetParent(_editorPreview.transform);
            obj.transform.position = data.position;
            obj.transform.localScale = new Vector3(data.size.x, data.size.y, 1);

            SpriteRenderer renderer = obj.AddComponent<SpriteRenderer>();
            renderer.color = GetObstacleColor(data.obstacleType);
            renderer.sortingOrder = 50;

            Texture2D tex = new Texture2D(1, 1);
            tex.SetPixel(0, 0, renderer.color);
            tex.Apply();
            renderer.sprite = Sprite.Create(tex, new Rect(0, 0, 1, 1), new Vector2(0.5f, 0.5f));

            BoxCollider2D collider = obj.AddComponent<BoxCollider2D>();
            collider.isTrigger = true;

            obj.AddComponent<EditorObject>().Initialize(data, EditorObjectType.Obstacle);

            _editorObjects.Add(obj);
        }

        private void CreateEditorItem(ItemData data)
        {
            GameObject obj = new GameObject($"Editor_Item_{data.itemType}");
            obj.transform.SetParent(_editorPreview.transform);
            obj.transform.position = data.position;

            SpriteRenderer renderer = obj.AddComponent<SpriteRenderer>();
            renderer.color = GetItemColor(data.itemType);
            renderer.sortingOrder = 51;

            Texture2D tex = new Texture2D(16, 16);
            for (int y = 0; y < 16; y++)
            {
                for (int x = 0; x < 16; x++)
                {
                    float dist = Vector2.Distance(new Vector2(x, y), new Vector2(8, 8));
                    if (dist < 7)
                    {
                        tex.SetPixel(x, y, renderer.color);
                    }
                    else
                    {
                        tex.SetPixel(x, y, Color.clear);
                    }
                }
            }
            tex.Apply();
            renderer.sprite = Sprite.Create(tex, new Rect(0, 0, 16, 16), new Vector2(0.5f, 0.5f));
            obj.transform.localScale = Vector3.one;

            CircleCollider2D collider = obj.AddComponent<CircleCollider2D>();
            collider.radius = 0.3f;
            collider.isTrigger = true;

            obj.AddComponent<EditorObject>().Initialize(data, EditorObjectType.Item);

            _editorObjects.Add(obj);
        }

        private void CreateEditorGravityZone(GravityZoneData data)
        {
            GameObject obj = new GameObject($"Editor_GravityZone");
            obj.transform.SetParent(_editorPreview.transform);
            obj.transform.position = data.position;
            obj.transform.localScale = new Vector3(data.size.x, data.size.y, 1);

            SpriteRenderer renderer = obj.AddComponent<SpriteRenderer>();
            renderer.color = new Color(0.5f, 0.5f, 1f, 0.3f);
            renderer.sortingOrder = 40;

            Texture2D tex = new Texture2D(1, 1);
            tex.SetPixel(0, 0, renderer.color);
            tex.Apply();
            renderer.sprite = Sprite.Create(tex, new Rect(0, 0, 1, 1), new Vector2(0.5f, 0.5f));

            BoxCollider2D collider = obj.AddComponent<BoxCollider2D>();
            collider.isTrigger = true;

            obj.AddComponent<EditorObject>().Initialize(data, EditorObjectType.GravityZone);

            _editorObjects.Add(obj);
        }

        private void CreateEditorStartPosition()
        {
            GameObject obj = new GameObject("Editor_StartPosition");
            obj.transform.SetParent(_editorPreview.transform);
            obj.transform.position = _editingLevel.playerStart.position;

            SpriteRenderer renderer = obj.AddComponent<SpriteRenderer>();
            renderer.color = Color.green;
            renderer.sortingOrder = 52;

            Texture2D tex = new Texture2D(16, 16);
            for (int y = 0; y < 16; y++)
            {
                for (int x = 0; x < 16; x++)
                {
                    float dist = Vector2.Distance(new Vector2(x, y), new Vector2(8, 8));
                    if (dist < 7)
                    {
                        tex.SetPixel(x, y, Color.green);
                    }
                    else
                    {
                        tex.SetPixel(x, y, Color.clear);
                    }
                }
            }
            tex.Apply();
            renderer.sprite = Sprite.Create(tex, new Rect(0, 0, 16, 16), new Vector2(0.5f, 0.5f));

            obj.AddComponent<EditorObject>().Initialize(_editingLevel.playerStart, EditorObjectType.StartPosition);

            _editorObjects.Add(obj);
        }

        private void CreateEditorGoal()
        {
            GameObject obj = new GameObject("Editor_Goal");
            obj.transform.SetParent(_editorPreview.transform);
            obj.transform.position = _editingLevel.goal.position;
            obj.transform.localScale = new Vector3(_editingLevel.goal.size.x, _editingLevel.goal.size.y, 1);

            SpriteRenderer renderer = obj.AddComponent<SpriteRenderer>();
            renderer.color = new Color(1f, 0.8f, 0f, 0.6f);
            renderer.sortingOrder = 52;

            Texture2D tex = new Texture2D(1, 1);
            tex.SetPixel(0, 0, renderer.color);
            tex.Apply();
            renderer.sprite = Sprite.Create(tex, new Rect(0, 0, 1, 1), new Vector2(0.5f, 0.5f));

            BoxCollider2D collider = obj.AddComponent<BoxCollider2D>();
            collider.isTrigger = true;

            obj.AddComponent<EditorObject>().Initialize(_editingLevel.goal, EditorObjectType.Goal);

            _editorObjects.Add(obj);
        }

        private Color GetObstacleColor(ObstacleType type)
        {
            switch (type)
            {
                case ObstacleType.StaticPlatform:
                    return new Color(0.6f, 0.6f, 0.6f);
                case ObstacleType.MovingPlatform:
                    return new Color(0.3f, 0.6f, 0.9f);
                case ObstacleType.BouncingPad:
                    return new Color(0.9f, 0.6f, 0.3f);
                case ObstacleType.SpikeTrap:
                    return new Color(0.9f, 0.2f, 0.2f);
                case ObstacleType.Portal:
                    return new Color(0.8f, 0.3f, 0.9f);
                default:
                    return Color.gray;
            }
        }

        private Color GetItemColor(ItemType type)
        {
            switch (type)
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

        public void SetMode(EditorMode mode)
        {
            _currentMode = mode;
            OnEditorModeChanged?.Invoke(_currentMode);

            if (_previewObject != null)
            {
                Destroy(_previewObject);
                _previewObject = null;
            }
        }

        public void SelectObstacleType(ObstacleType type)
        {
            _selectedObstacleType = type;
        }

        public void SelectItemType(ItemType type)
        {
            _selectedItemType = type;
        }

        public void SetGridSize(float size)
        {
            _gridSize = Mathf.Max(0.1f, size);
        }

        public void SetObjectSize(Vector2 size)
        {
            _currentObjectSize = size;
        }

        public void SetGravityScale(float scale)
        {
            _currentGravityScale = scale;
        }

        public void SetGravityDirection(GravityDirection direction)
        {
            _selectedGravityDirection = direction;
        }

        private void Update()
        {
            if (!IsEditing) return;

            HandleInput();
            UpdatePreviewPosition();
        }

        private void HandleInput()
        {
            Vector2 mousePos = Camera.main != null
                ? Camera.main.ScreenToWorldPoint(Input.mousePosition)
                : Vector2.zero;

            _currentMousePosition = _snapToGrid ? SnapToGrid(mousePos) : mousePos;

            if (Input.GetMouseButtonDown(0))
            {
                OnLeftClick();
            }

            if (Input.GetMouseButtonDown(1))
            {
                OnRightClick();
            }

            if (Input.GetKey(KeyCode.LeftShift) && Input.GetMouseButton(0))
            {
                HandleDragging();
            }
        }

        private void OnLeftClick()
        {
            switch (_currentMode)
            {
                case EditorMode.PlaceObstacle:
                    PlaceObstacle();
                    break;
                case EditorMode.PlaceItem:
                    PlaceItem();
                    break;
                case EditorMode.PlaceGravityZone:
                    PlaceGravityZone();
                    break;
                case EditorMode.SetStartPosition:
                    SetStartPosition();
                    break;
                case EditorMode.SetGoalPosition:
                    SetGoalPosition();
                    break;
                case EditorMode.Erase:
                    EraseAtPosition();
                    break;
            }
        }

        private void OnRightClick()
        {
            _isDragging = false;
            _startDragPosition = Vector2.zero;
        }

        private void HandleDragging()
        {
            if (!_isDragging)
            {
                _isDragging = true;
                _startDragPosition = _currentMousePosition;
            }
        }

        private Vector2 SnapToGrid(Vector2 position)
        {
            return new Vector2(
                Mathf.Round(position.x / _gridSize) * _gridSize,
                Mathf.Round(position.y / _gridSize) * _gridSize);
        }

        private void UpdatePreviewPosition()
        {
            if (_previewObject == null)
            {
                CreatePreviewObject();
            }

            if (_previewObject != null)
            {
                _previewObject.transform.position = _currentMousePosition;
            }
        }

        private void CreatePreviewObject()
        {
            if (_currentMode == EditorMode.PlaceObstacle)
            {
                _previewObject = GameObject.CreatePrimitive(PrimitiveType.Cube);
                _previewObject.name = "Preview_Obstacle";
                _previewObject.transform.localScale = new Vector3(_currentObjectSize.x, _currentObjectSize.y, 0.1f);
                SpriteRenderer renderer = _previewObject.AddComponent<SpriteRenderer>();
                renderer.color = new Color(1f, 1f, 1f, 0.5f);
            }
            else if (_currentMode == EditorMode.PlaceItem)
            {
                _previewObject = GameObject.CreatePrimitive(PrimitiveType.Sphere);
                _previewObject.name = "Preview_Item";
                _previewObject.transform.localScale = Vector3.one * 0.5f;
            }
            else if (_currentMode == EditorMode.PlaceGravityZone)
            {
                _previewObject = GameObject.CreatePrimitive(PrimitiveType.Cube);
                _previewObject.name = "Preview_GravityZone";
                _previewObject.transform.localScale = new Vector3(3f, 3f, 0.1f);
                SpriteRenderer renderer = _previewObject.AddComponent<SpriteRenderer>();
                renderer.color = new Color(0.5f, 0.5f, 1f, 0.3f);
            }
            else
            {
                return;
            }

            Collider[] colliders = _previewObject.GetComponents<Collider>();
            foreach (var collider in colliders)
            {
                Destroy(collider);
            }
        }

        private void PlaceObstacle()
        {
            ObstacleData newObstacle = new ObstacleData
            {
                obstacleType = _selectedObstacleType,
                position = _currentMousePosition,
                size = _currentObjectSize,
                isDynamic = false
            };

            _editingLevel.obstacles.Add(newObstacle);
            CreateEditorObstacle(newObstacle);
            OnLevelModified?.Invoke(_editingLevel);
        }

        private void PlaceItem()
        {
            ItemData newItem = new ItemData
            {
                itemType = _selectedItemType,
                position = _currentMousePosition,
                value = 1,
                isCollected = false
            };

            _editingLevel.items.Add(newItem);
            CreateEditorItem(newItem);
            OnLevelModified?.Invoke(_editingLevel);
        }

        private void PlaceGravityZone()
        {
            GravityZoneData newZone = new GravityZoneData
            {
                position = _currentMousePosition,
                size = _currentObjectSize * 2f,
                gravityScale = _currentGravityScale,
                gravityDirection = _selectedGravityDirection
            };

            _editingLevel.gravityZones.Add(newZone);
            CreateEditorGravityZone(newZone);
            OnLevelModified?.Invoke(_editingLevel);
        }

        private void SetStartPosition()
        {
            _editingLevel.playerStart.position = _currentMousePosition;
            RefreshEditorObjects();
            OnLevelModified?.Invoke(_editingLevel);
        }

        private void SetGoalPosition()
        {
            _editingLevel.goal.position = _currentMousePosition;
            RefreshEditorObjects();
            OnLevelModified?.Invoke(_editingLevel);
        }

        private void EraseAtPosition()
        {
            for (int i = _editorObjects.Count - 1; i >= 0; i--)
            {
                GameObject obj = _editorObjects[i];
                EditorObject editorObj = obj.GetComponent<EditorObject>();

                if (editorObj != null)
                {
                    float distance = Vector2.Distance(obj.transform.position, _currentMousePosition);
                    if (distance < 1f)
                    {
                        RemoveEditorObject(editorObj);
                        break;
                    }
                }
            }
        }

        private void RemoveEditorObject(EditorObject editorObj)
        {
            switch (editorObj.ObjectType)
            {
                case EditorObjectType.Obstacle:
                    _editingLevel.obstacles.Remove(editorObj.ObstacleData);
                    break;
                case EditorObjectType.Item:
                    _editingLevel.items.Remove(editorObj.ItemData);
                    break;
                case EditorObjectType.GravityZone:
                    _editingLevel.gravityZones.Remove(editorObj.GravityZoneData);
                    break;
            }

            _editorObjects.Remove(editorObj.gameObject);
            Destroy(editorObj.gameObject);
            OnLevelModified?.Invoke(_editingLevel);
        }

        public void SaveLevel()
        {
            if (_editingLevel == null) return;

            LevelDataManager.Instance.SaveLevel(_editingLevel);
            OnLevelSaved?.Invoke(_editingLevel);
        }

        public void LoadLevel(int levelId)
        {
            LevelData level = LevelDataManager.Instance.GetLevelById(levelId);
            if (level != null)
            {
                StartEditing(level);
            }
        }

        public void NewLevel(string levelName)
        {
            LevelData newLevel = LevelDataManager.Instance.CreateNewLevel(levelName);
            StartEditing(newLevel);
        }

        public void DeleteLevel()
        {
            if (_editingLevel == null) return;

            LevelDataManager.Instance.DeleteLevel(_editingLevel.levelId);
            StopEditing();
        }

        public void TestLevel()
        {
            if (_editingLevel == null) return;

            LevelValidator.ValidationResult result = LevelValidator.Instance.ValidateLevel(_editingLevel);
            if (result.IsValid)
            {
                StopEditing();
                GameManager.Instance.StartGame(_editingLevel.levelId);
            }
        }

        public void ClearEditorObjects()
        {
            foreach (var obj in _editorObjects)
            {
                if (obj != null)
                {
                    Destroy(obj);
                }
            }
            _editorObjects.Clear();
        }

        private void ClearEditorPreview()
        {
            ClearEditorObjects();

            if (_editorPreview != null)
            {
                Destroy(_editorPreview);
                _editorPreview = null;
            }

            if (_previewObject != null)
            {
                Destroy(_previewObject);
                _previewObject = null;
            }
        }

        public void SetLevelBounds(Vector2 bounds)
        {
            if (_editingLevel != null)
            {
                _editingLevel.levelBounds = bounds;
                CreateEditorPreview();
                OnLevelModified?.Invoke(_editingLevel);
            }
        }

        public void SetGlobalGravityScale(float scale)
        {
            if (_editingLevel != null)
            {
                _editingLevel.globalGravityScale = scale;
                OnLevelModified?.Invoke(_editingLevel);
            }
        }

        public void SetGlobalGravityDirection(GravityDirection direction)
        {
            if (_editingLevel != null)
            {
                _editingLevel.globalGravityDirection = direction;
                OnLevelModified?.Invoke(_editingLevel);
            }
        }

        public void SetTimeLimit(float timeLimit)
        {
            if (_editingLevel != null)
            {
                _editingLevel.timeLimit = timeLimit;
                OnLevelModified?.Invoke(_editingLevel);
            }
        }

        public void SetRequiredItems(int count)
        {
            if (_editingLevel != null)
            {
                _editingLevel.goal.requiredItems = count;
                OnLevelModified?.Invoke(_editingLevel);
            }
        }

        public void Cleanup()
        {
            StopEditing();
        }
    }

    public enum EditorObjectType
    {
        Obstacle,
        Item,
        GravityZone,
        StartPosition,
        Goal
    }

    public class EditorObject : MonoBehaviour
    {
        public EditorObjectType ObjectType;
        public ObstacleData ObstacleData;
        public ItemData ItemData;
        public GravityZoneData GravityZoneData;
        public PlayerStartData PlayerStartData;
        public GoalData GoalData;

        public void Initialize(object data, EditorObjectType type)
        {
            ObjectType = type;

            switch (type)
            {
                case EditorObjectType.Obstacle:
                    ObstacleData = data as ObstacleData;
                    break;
                case EditorObjectType.Item:
                    ItemData = data as ItemData;
                    break;
                case EditorObjectType.GravityZone:
                    GravityZoneData = data as GravityZoneData;
                    break;
                case EditorObjectType.StartPosition:
                    PlayerStartData = data as PlayerStartData;
                    break;
                case EditorObjectType.Goal:
                    GoalData = data as GoalData;
                    break;
            }
        }
    }
}
