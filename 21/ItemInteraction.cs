using System;
using System.Collections.Generic;
using UnityEngine;

namespace PhysicsPuzzleGame
{
    public class ItemInteraction : MonoBehaviour
    {
        private static ItemInteraction _instance;
        public static ItemInteraction Instance
        {
            get
            {
                if (_instance == null)
                {
                    GameObject go = new GameObject("ItemInteraction");
                    _instance = go.AddComponent<ItemInteraction>();
                    DontDestroyOnLoad(go);
                }
                return _instance;
            }
        }

        private Dictionary<ItemType, Action<PlayerController, ItemData>> _itemHandlers
            = new Dictionary<ItemType, Action<PlayerController, ItemData>>();

        private List<Item> _activeItems = new List<Item>();
        private Dictionary<ItemType, int> _collectedItems = new Dictionary<ItemType, int>();

        public event Action<Item> OnItemCollected;
        public event Action<ItemType, int> OnItemUsed;

        public void Initialize()
        {
            RegisterDefaultItemHandlers();
        }

        private void RegisterDefaultItemHandlers()
        {
            RegisterItemHandler(ItemType.Coin, HandleCoinCollected);
            RegisterItemHandler(ItemType.Key, HandleKeyCollected);
            RegisterItemHandler(ItemType.Booster, HandleBoosterCollected);
            RegisterItemHandler(ItemType.Shield, HandleShieldCollected);
            RegisterItemHandler(ItemType.TimeBonus, HandleTimeBonusCollected);
        }

        public void RegisterItemHandler(ItemType itemType, Action<PlayerController, ItemData> handler)
        {
            if (!_itemHandlers.ContainsKey(itemType))
            {
                _itemHandlers[itemType] = handler;
            }
            else
            {
                _itemHandlers[itemType] += handler;
            }
        }

        public void UnregisterItemHandler(ItemType itemType, Action<PlayerController, ItemData> handler)
        {
            if (_itemHandlers.ContainsKey(itemType))
            {
                _itemHandlers[itemType] -= handler;
            }
        }

        public void CollectItem(Item item, PlayerController player)
        {
            if (item == null || player == null) return;
            if (item.IsCollected) return;

            item.Collect();

            if (_itemHandlers.ContainsKey(item.ItemData.itemType))
            {
                _itemHandlers[item.ItemData.itemType]?.Invoke(player, item.ItemData);
            }

            if (!_collectedItems.ContainsKey(item.ItemData.itemType))
            {
                _collectedItems[item.ItemData.itemType] = 0;
            }
            _collectedItems[item.ItemData.itemType]++;

            OnItemCollected?.Invoke(item);
            _activeItems.Remove(item);
        }

        private void HandleCoinCollected(PlayerController player, ItemData data)
        {
            if (player != null)
            {
                player.AddScore(data.value);
            }
        }

        private void HandleKeyCollected(PlayerController player, ItemData data)
        {
            if (player != null)
            {
                player.AddKey();
            }
        }

        private void HandleBoosterCollected(PlayerController player, ItemData data)
        {
            if (player != null)
            {
                player.ActivateSpeedBoost(data.value);
            }
        }

        private void HandleShieldCollected(PlayerController player, ItemData data)
        {
            if (player != null)
            {
                player.ActivateShield(data.value);
            }
        }

        private void HandleTimeBonusCollected(PlayerController player, ItemData data)
        {
            if (GameManager.Instance != null)
            {
                GameManager.Instance.AddTime(data.value);
            }
        }

        public bool UseItem(ItemType itemType, PlayerController player)
        {
            if (!HasItem(itemType)) return false;

            _collectedItems[itemType]--;

            switch (itemType)
            {
                case ItemType.Key:
                    player.UseKey();
                    break;
                case ItemType.Shield:
                    player.ActivateShield(5f);
                    break;
                case ItemType.Booster:
                    player.ActivateSpeedBoost(5f);
                    break;
            }

            OnItemUsed?.Invoke(itemType, _collectedItems[itemType]);
            return true;
        }

        public bool HasItem(ItemType itemType)
        {
            return _collectedItems.ContainsKey(itemType) && _collectedItems[itemType] > 0;
        }

        public int GetItemCount(ItemType itemType)
        {
            return _collectedItems.ContainsKey(itemType) ? _collectedItems[itemType] : 0;
        }

        public Dictionary<ItemType, int> GetAllCollectedItems()
        {
            return new Dictionary<ItemType, int>(_collectedItems);
        }

        public void RegisterActiveItem(Item item)
        {
            if (!_activeItems.Contains(item))
            {
                _activeItems.Add(item);
            }
        }

        public void UnregisterActiveItem(Item item)
        {
            _activeItems.Remove(item);
        }

        public List<Item> GetActiveItems()
        {
            return _activeItems;
        }

        public Item GetNearestItem(Vector2 position, float maxDistance = 2f)
        {
            Item nearest = null;
            float minDistance = maxDistance;

            foreach (var item in _activeItems)
            {
                if (item == null || item.IsCollected) continue;

                float distance = Vector2.Distance(position, item.transform.position);
                if (distance < minDistance)
                {
                    minDistance = distance;
                    nearest = item;
                }
            }

            return nearest;
        }

        public List<Item> GetItemsInRange(Vector2 position, float radius)
        {
            List<Item> itemsInRange = new List<Item>();

            foreach (var item in _activeItems)
            {
                if (item == null || item.IsCollected) continue;

                float distance = Vector2.Distance(position, item.transform.position);
                if (distance <= radius)
                {
                    itemsInRange.Add(item);
                }
            }

            return itemsInRange;
        }

        public void ClearAllItems()
        {
            foreach (var item in _activeItems)
            {
                if (item != null)
                    Destroy(item.gameObject);
            }
            _activeItems.Clear();
            _collectedItems.Clear();
        }

        public void ResetItemCounters()
        {
            _collectedItems.Clear();
        }

        public void Cleanup()
        {
            ClearAllItems();
            _itemHandlers.Clear();
            ResetItemCounters();
        }
    }

    public class Item : MonoBehaviour
    {
        private ItemData _itemData;
        private bool _isCollected = false;
        private float _floatOffset = 0f;
        private float _floatSpeed = 2f;
        private float _rotationSpeed = 90f;

        public ItemData ItemData => _itemData;
        public bool IsCollected => _isCollected;

        public event Action<Item> OnCollected;

        public void Initialize(ItemData data)
        {
            _itemData = data;
            _floatOffset = UnityEngine.Random.Range(0f, Mathf.PI * 2f);
        }

        private void Start()
        {
            ItemInteraction.Instance.RegisterActiveItem(this);
        }

        private void Update()
        {
            if (_isCollected) return;

            float floatY = Mathf.Sin(Time.time * _floatSpeed + _floatOffset) * 0.1f;
            transform.position = new Vector3(
                _itemData.position.x,
                _itemData.position.y + floatY,
                0);

            transform.Rotate(0, 0, _rotationSpeed * Time.deltaTime);
        }

        private void OnTriggerEnter2D(Collider2D other)
        {
            if (_isCollected) return;

            if (other.CompareTag("Player"))
            {
                PlayerController player = other.GetComponent<PlayerController>();
                if (player != null)
                {
                    ItemInteraction.Instance.CollectItem(this, player);
                }
            }
        }

        public void Collect()
        {
            _isCollected = true;
            _itemData.isCollected = true;
            OnCollected?.Invoke(this);

            SpriteRenderer renderer = GetComponent<SpriteRenderer>();
            if (renderer != null)
            {
                renderer.enabled = false;
            }

            Collider2D collider = GetComponent<Collider2D>();
            if (collider != null)
            {
                collider.enabled = false;
            }

            Destroy(gameObject, 0.1f);
        }

        private void OnDestroy()
        {
            ItemInteraction.Instance.UnregisterActiveItem(this);
        }
    }

    public class PhysicsProp : MonoBehaviour
    {
        private Rigidbody2D _rb;
        private Collider2D _collider;
        private SpriteRenderer _renderer;

        public bool IsGravityAffected = true;
        public bool IsPushable = true;
        public float Mass = 1f;
        public float Friction = 0.6f;
        public float Bounciness = 0.1f;

        private Vector3 _initialPosition;
        private bool _isResetting = false;

        public event Action<PhysicsProp> OnPropMoved;
        public event Action<PhysicsProp> OnPropReset;

        public Rigidbody2D Rigidbody => _rb;

        private void Awake()
        {
            _rb = GetComponent<Rigidbody2D>();
            if (_rb == null)
            {
                _rb = gameObject.AddComponent<Rigidbody2D>();
            }

            _collider = GetComponent<Collider2D>();
            if (_collider == null)
            {
                _collider = gameObject.AddComponent<BoxCollider2D>();
            }

            _renderer = GetComponent<SpriteRenderer>();

            InitializePhysicsProperties();
            _initialPosition = transform.position;
        }

        private void InitializePhysicsProperties()
        {
            _rb.mass = Mass;
            _rb.bodyType = IsPushable ? RigidbodyType2D.Dynamic : RigidbodyType2D.Kinematic;
            _rb.gravityScale = IsGravityAffected ? 1f : 0f;

            PhysicsMaterial2D material = new PhysicsMaterial2D("PropMaterial");
            material.friction = Friction;
            material.bounciness = Bounciness;
            _collider.sharedMaterial = material;
        }

        public void ApplyForce(Vector2 force, ForceMode2D mode = ForceMode2D.Force)
        {
            if (_rb != null && IsPushable)
            {
                _rb.AddForce(force, mode);
                OnPropMoved?.Invoke(this);
            }
        }

        public void ApplyImpulse(Vector2 impulse)
        {
            if (_rb != null && IsPushable)
            {
                _rb.AddForce(impulse, ForceMode2D.Impulse);
                OnPropMoved?.Invoke(this);
            }
        }

        public void SetVelocity(Vector2 velocity)
        {
            if (_rb != null)
            {
                _rb.velocity = velocity;
            }
        }

        public void ResetPosition()
        {
            _isResetting = true;
            transform.position = _initialPosition;
            if (_rb != null)
            {
                _rb.velocity = Vector2.zero;
                _rb.angularVelocity = 0f;
            }
            OnPropReset?.Invoke(this);
            _isResetting = false;
        }

        public void Freeze()
        {
            if (_rb != null)
            {
                _rb.constraints = RigidbodyConstraints2D.FreezeAll;
            }
        }

        public void Unfreeze()
        {
            if (_rb != null)
            {
                _rb.constraints = RigidbodyConstraints2D.None;
            }
        }

        public void SetGravityAffected(bool affected)
        {
            IsGravityAffected = affected;
            if (_rb != null)
            {
                _rb.gravityScale = affected ? 1f : 0f;
            }
        }

        public void SetPushable(bool pushable)
        {
            IsPushable = pushable;
            if (_rb != null)
            {
                _rb.bodyType = pushable ? RigidbodyType2D.Dynamic : RigidbodyType2D.Kinematic;
            }
        }

        private void OnCollisionEnter2D(Collision2D collision)
        {
            if (_isResetting) return;

            if (collision.gameObject.CompareTag("Player") && IsPushable)
            {
                Vector2 pushForce = (transform.position - collision.transform.position).normalized * 5f;
                ApplyForce(pushForce, ForceMode2D.Impulse);
            }
        }

        public void CreatePhysicsProp(Vector2 position, Vector2 size, float mass = 1f)
        {
            transform.position = position;
            Mass = mass;

            if (_collider is BoxCollider2D boxCollider)
            {
                boxCollider.size = size;
            }

            if (_renderer != null)
            {
                _renderer.color = new Color(0.8f, 0.6f, 0.4f);
                Texture2D tex = new Texture2D(1, 1);
                tex.SetPixel(0, 0, _renderer.color);
                tex.Apply();
                _renderer.sprite = Sprite.Create(tex, new Rect(0, 0, 1, 1), new Vector2(0.5f, 0.5f));
                transform.localScale = new Vector3(size.x, size.y, 1);
            }

            InitializePhysicsProperties();
            _initialPosition = position;
        }
    }
}
