using System;
using System.Collections.Generic;
using UnityEngine;

namespace PhysicsPuzzleGame
{
    public class PlayerController : MonoBehaviour
    {
        private static PlayerController _instance;
        public static PlayerController Instance => _instance;

        [Header("移动设置")]
        public float moveSpeed = 5f;
        public float jumpForce = 10f;
        public float doubleJumpForce = 8f;
        public float maxFallSpeed = -20f;
        public float maxHorizontalSpeed = 15f;
        public float maxVerticalSpeed = 25f;

        [Header("物理属性")]
        public int maxHealth = 3;
        public float invincibilityTime = 2f;
        public float respawnDelay = 1f;

        [Header("能力状态")]
        public bool canDoubleJump = true;
        public bool canWallJump = true;
        public bool canCoyoteTime = true;

        [Header("检测设置")]
        public float groundCheckDistance = 0.1f;
        public float wallCheckDistance = 0.1f;
        public float coyoteTimeDuration = 0.1f;

        private Rigidbody2D _rb;
        private Collider2D _collider;
        private SpriteRenderer _renderer;
        private CollisionDetector _collisionDetector;

        private Vector2 _moveInput;
        private bool _isGrounded;
        private bool _isTouchingWall;
        private bool _isJumping;
        private bool _canDoubleJump;
        private bool _isInvincible;
        private bool _isAlive = true;

        private float _currentHealth;
        private int _currentScore;
        private int _coinCount;
        private int _keyCount;

        private float _coyoteTimeCounter;
        private float _invincibilityCounter;
        private Vector2 _startPosition;

        private float _speedBoostTimer;
        private float _shieldTimer;
        private bool _hasShield;

        public event Action<int> OnHealthChanged;
        public event Action<int> OnScoreChanged;
        public event Action<int> OnCoinCollected;
        public event Action<int> OnKeyCollected;
        public event Action OnPlayerDied;
        public event Action OnPlayerRespawned;
        public event Action<bool> OnGroundedChanged;

        public int CurrentHealth => (int)_currentHealth;
        public int Health => (int)_currentHealth;
        public int CurrentScore => _currentScore;
        public int CoinCount => _coinCount;
        public int KeyCount => _keyCount;
        public bool IsGrounded => _isGrounded;
        public bool IsAlive => _isAlive;
        public bool HasShield => _hasShield;
        public bool IsJumping => _isJumping;

        private void Awake()
        {
            if (_instance == null)
            {
                _instance = this;
            }
        }

        private void Start()
        {
            InitializeComponents();
        }

        private void InitializeComponents()
        {
            _rb = GetComponent<Rigidbody2D>();
            if (_rb == null)
            {
                _rb = gameObject.AddComponent<Rigidbody2D>();
            }
            _rb.freezeRotation = true;
            _rb.collisionDetectionMode = CollisionDetectionMode2D.Continuous;
            _rb.interpolation = RigidbodyInterpolation2D.Interpolate;
            _rb.sleepMode = RigidbodySleepMode2D.NeverSleep;

            _collider = GetComponent<Collider2D>();
            if (_collider == null)
            {
                _collider = gameObject.AddComponent<BoxCollider2D>();
            }
            if (_collider is BoxCollider2D boxCollider)
            {
                boxCollider.size = new Vector2(0.6f, 0.8f);
            }

            _renderer = GetComponent<SpriteRenderer>();
            if (_renderer == null)
            {
                _renderer = gameObject.AddComponent<SpriteRenderer>();
                CreatePlayerVisual();
            }

            _collisionDetector = GetComponent<CollisionDetector>();
            if (_collisionDetector == null)
            {
                _collisionDetector = gameObject.AddComponent<CollisionDetector>();
            }

            gameObject.tag = "Player";
            gameObject.layer = (int)PhysicsLayer.Player;

            _currentHealth = maxHealth;
            _startPosition = transform.position;
        }

        private void CreatePlayerVisual()
        {
            Texture2D tex = new Texture2D(32, 32);
            for (int y = 0; y < 32; y++)
            {
                for (int x = 0; x < 32; x++)
                {
                    float dist = Vector2.Distance(new Vector2(x, y), new Vector2(16, 16));
                    if (dist < 14)
                    {
                        Color playerColor = new Color(0.2f, 0.6f, 1f);
                        tex.SetPixel(x, y, playerColor);
                    }
                    else if (dist < 16)
                    {
                        tex.SetPixel(x, y, new Color(0.1f, 0.4f, 0.8f));
                    }
                    else
                    {
                        tex.SetPixel(x, y, Color.clear);
                    }
                }
            }
            tex.Apply();
            _renderer.sprite = Sprite.Create(tex, new Rect(0, 0, 32, 32), new Vector2(0.5f, 0.5f));
            _renderer.color = new Color(0.2f, 0.6f, 1f);
            transform.localScale = new Vector3(0.8f, 0.8f, 1);
        }

        private void Update()
        {
            if (!_isAlive) return;

            HandleInput();
            UpdateTimers();
            CheckGrounded();
        }

        private void FixedUpdate()
        {
            if (!_isAlive) return;

            ApplyMovement();
            ApplyGravity();
            ClampVelocity();
            PreventTunneling();
            CheckBoundary();
        }

        private void HandleInput()
        {
            _moveInput.x = Input.GetAxisRaw("Horizontal");
            _moveInput.y = Input.GetAxisRaw("Vertical");

            if (Input.GetButtonDown("Jump"))
            {
                TryJump();
            }

            if (Input.GetButtonUp("Jump") && _rb.velocity.y > 0)
            {
                _rb.velocity = new Vector2(_rb.velocity.x, _rb.velocity.y * 0.5f);
            }
        }

        private void UpdateTimers()
        {
            if (_coyoteTimeCounter > 0)
            {
                _coyoteTimeCounter -= Time.deltaTime;
            }

            if (_invincibilityCounter > 0)
            {
                _invincibilityCounter -= Time.deltaTime;
                if (_invincibilityCounter <= 0)
                {
                    _isInvincible = false;
                    if (_renderer != null)
                    {
                        _renderer.color = new Color(0.2f, 0.6f, 1f);
                    }
                }
            }

            if (_speedBoostTimer > 0)
            {
                _speedBoostTimer -= Time.deltaTime;
            }

            if (_shieldTimer > 0)
            {
                _shieldTimer -= Time.deltaTime;
                if (_shieldTimer <= 0)
                {
                    _hasShield = false;
                }
            }
        }

        private void CheckGrounded()
        {
            bool wasGrounded = _isGrounded;

            PhysicsCollision2D collisionSystem = PhysicsCollision2D.Instance;
            _isGrounded = collisionSystem.IsGrounded(_rb, groundCheckDistance);
            _isTouchingWall = collisionSystem.IsTouchingWall(_rb, wallCheckDistance);

            if (_isGrounded)
            {
                _isJumping = false;
                _canDoubleJump = canDoubleJump;
                _coyoteTimeCounter = coyoteTimeDuration;
            }

            if (wasGrounded != _isGrounded)
            {
                OnGroundedChanged?.Invoke(_isGrounded);
            }
        }

        private void TryJump()
        {
            if (_isGrounded || _coyoteTimeCounter > 0)
            {
                _rb.velocity = new Vector2(_rb.velocity.x, jumpForce);
                _isJumping = true;
                _isGrounded = false;
                _coyoteTimeCounter = 0;
            }
            else if (_canDoubleJump)
            {
                _rb.velocity = new Vector2(_rb.velocity.x, doubleJumpForce);
                _canDoubleJump = false;
            }
        }

        private void ApplyMovement()
        {
            float currentSpeed = moveSpeed;
            if (_speedBoostTimer > 0)
            {
                currentSpeed *= 1.5f;
            }

            _rb.velocity = new Vector2(_moveInput.x * currentSpeed, _rb.velocity.y);

            if (_moveInput.x != 0 && _renderer != null)
            {
                _renderer.flipX = _moveInput.x < 0;
            }
        }

        private void ApplyGravity()
        {
            if (_rb.velocity.y < maxFallSpeed)
            {
                _rb.velocity = new Vector2(_rb.velocity.x, maxFallSpeed);
            }
        }

        private void ClampVelocity()
        {
            float clampedX = Mathf.Clamp(_rb.velocity.x, -maxHorizontalSpeed, maxHorizontalSpeed);
            float clampedY = Mathf.Clamp(_rb.velocity.y, -maxVerticalSpeed, maxVerticalSpeed);
            _rb.velocity = new Vector2(clampedX, clampedY);
        }

        private void PreventTunneling()
        {
            if (_collider == null || _rb == null) return;

            Vector2 velocity = _rb.velocity;
            float speed = velocity.magnitude;

            if (speed > 5f)
            {
                float distance = speed * Time.fixedDeltaTime;
                Vector2 direction = velocity.normalized;

                RaycastHit2D hit = Physics2D.CapsuleCast(
                    _rb.position,
                    _collider.bounds.size,
                    CapsuleDirection2D.Vertical,
                    0f,
                    direction,
                    distance,
                    LayerMask.GetMask("Platform", "MovingPlatform", "Hazard"));

                if (hit.collider != null)
                {
                    float penetrationDepth = Vector2.Distance(_rb.position, hit.point) - 0.1f;
                    if (penetrationDepth > 0)
                    {
                        _rb.position = hit.point - direction * Mathf.Min(penetrationDepth, distance);
                    }
                }
            }
        }

        private void CheckBoundary()
        {
            LevelData currentLevel = LevelGenerator.Instance.CurrentLevelData;
            if (currentLevel != null)
            {
                if (!currentLevel.IsPositionInLevelBounds(transform.position))
                {
                    TakeDamage();
                    Respawn();
                }
            }
        }

        public void TakeDamage(int damage = 1)
        {
            if (_isInvincible || !_isAlive) return;

            if (_hasShield)
            {
                _hasShield = false;
                _shieldTimer = 0;
                return;
            }

            _currentHealth -= damage;
            _isInvincible = true;
            _invincibilityCounter = invincibilityTime;

            if (_renderer != null)
            {
                _renderer.color = Color.red;
            }

            OnHealthChanged?.Invoke((int)_currentHealth);

            if (_currentHealth <= 0)
            {
                Die();
            }
        }

        private void Die()
        {
            _isAlive = false;
            _rb.velocity = Vector2.zero;
            _rb.bodyType = RigidbodyType2D.Kinematic;
            _collider.enabled = false;

            if (_renderer != null)
            {
                _renderer.enabled = false;
            }

            OnPlayerDied?.Invoke();

            SaveManager.Instance?.RecordDeath();

            Invoke(nameof(Respawn), respawnDelay);
        }

        public void Respawn()
        {
            _isAlive = true;
            _currentHealth = maxHealth;
            _isInvincible = true;
            _invincibilityCounter = invincibilityTime;
            _rb.bodyType = RigidbodyType2D.Dynamic;
            _collider.enabled = true;

            if (_renderer != null)
            {
                _renderer.enabled = true;
                _renderer.color = Color.white;
            }

            transform.position = _startPosition;
            _rb.velocity = Vector2.zero;

            OnPlayerRespawned?.Invoke();
            OnHealthChanged?.Invoke((int)_currentHealth);
        }

        public void TeleportTo(Vector2 position)
        {
            transform.position = position;
            _rb.velocity = Vector2.zero;
        }

        public void AddScore(int score)
        {
            _currentScore += score;
            OnScoreChanged?.Invoke(_currentScore);
        }

        public void AddCoin(int coins = 1)
        {
            _coinCount += coins;
            OnCoinCollected?.Invoke(_coinCount);
        }

        public void AddKey()
        {
            _keyCount++;
            OnKeyCollected?.Invoke(_keyCount);
        }

        public bool UseKey()
        {
            if (_keyCount > 0)
            {
                _keyCount--;
                return true;
            }
            return false;
        }

        public void ActivateSpeedBoost(float duration)
        {
            _speedBoostTimer = duration;
        }

        public void ActivateShield(float duration)
        {
            _hasShield = true;
            _shieldTimer = duration;
        }

        public void SetStartPosition(Vector2 position)
        {
            _startPosition = position;
            transform.position = position;
        }

        public void ResetPlayer()
        {
            _currentHealth = maxHealth;
            _currentScore = 0;
            _coinCount = 0;
            _keyCount = 0;
            _isAlive = true;
            _isInvincible = false;
            _speedBoostTimer = 0;
            _shieldTimer = 0;
            _hasShield = false;
            _canDoubleJump = canDoubleJump;

            transform.position = _startPosition;
            if (_rb != null)
            {
                _rb.velocity = Vector2.zero;
                _rb.bodyType = RigidbodyType2D.Dynamic;
            }
            if (_collider != null)
            {
                _collider.enabled = true;
            }
            if (_renderer != null)
            {
                _renderer.enabled = true;
                _renderer.color = new Color(0.2f, 0.6f, 1f);
            }

            OnHealthChanged?.Invoke((int)_currentHealth);
            OnScoreChanged?.Invoke(_currentScore);
            OnCoinCollected?.Invoke(_coinCount);
            OnKeyCollected?.Invoke(_keyCount);
        }

        public void ApplyKnockback(Vector2 direction, float force)
        {
            if (_rb != null)
            {
                _rb.AddForce(direction * force, ForceMode2D.Impulse);
            }
        }

        private void OnDestroy()
        {
            if (_instance == this)
            {
                _instance = null;
            }
        }
    }
}
