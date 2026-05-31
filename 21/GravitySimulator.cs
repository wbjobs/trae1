using System.Collections.Generic;
using UnityEngine;

namespace PhysicsPuzzleGame
{
    public class GravitySimulator : MonoBehaviour
    {
        private static GravitySimulator _instance;
        public static GravitySimulator Instance
        {
            get
            {
                if (_instance == null)
                {
                    GameObject go = new GameObject("GravitySimulator");
                    _instance = go.AddComponent<GravitySimulator>();
                    DontDestroyOnLoad(go);
                }
                return _instance;
            }
        }

        private LevelData _currentLevelData;
        private List<GravityZone> _activeGravityZones = new List<GravityZone>();
        private Vector2 _globalGravity;
        private bool _isGravityEnabled = true;
        private float _globalGravityScale = 1f;

        public Vector2 GlobalGravity => _globalGravity;
        public bool IsGravityEnabled => _isGravityEnabled;
        public float GlobalGravityScale => _globalGravityScale;

        private List<Rigidbody2D> _affectedRigidbodies = new List<Rigidbody2D>();

        public void Initialize(LevelData levelData)
        {
            _currentLevelData = levelData;
            _globalGravity = levelData.GetGravityVector();
            _globalGravityScale = levelData.globalGravityScale;
            CreateGravityZones(levelData.gravityZones);
            ApplyGlobalGravityToPhysics();
            ResetAllRigidbodyGravity();
        }

        private void ResetAllRigidbodyGravity()
        {
            Rigidbody2D[] allRigidbodies = FindObjectsOfType<Rigidbody2D>();
            foreach (var rb in allRigidbodies)
            {
                if (rb.bodyType == RigidbodyType2D.Dynamic)
                {
                    rb.gravityScale = 1f;
                    rb.velocity = Vector2.zero;
                }
            }
        }

        private void CreateGravityZones(List<GravityZoneData> zoneDataList)
        {
            ClearGravityZones();

            foreach (var zoneData in zoneDataList)
            {
                GameObject zoneObj = new GameObject("GravityZone");
                zoneObj.transform.position = zoneData.position;
                zoneObj.transform.SetParent(transform);

                GravityZone zone = zoneObj.AddComponent<GravityZone>();
                zone.Initialize(zoneData);
                _activeGravityZones.Add(zone);
            }
        }

        private void ClearGravityZones()
        {
            foreach (var zone in _activeGravityZones)
            {
                if (zone != null)
                    Destroy(zone.gameObject);
            }
            _activeGravityZones.Clear();
        }

        private void ApplyGlobalGravityToPhysics()
        {
            Physics2D.gravity = _globalGravity;
        }

        public void SetGlobalGravity(Vector2 gravity)
        {
            _globalGravity = gravity;
            Physics2D.gravity = _globalGravity;
            SyncAllRigidbodyGravity();
        }

        public void SetGlobalGravityScale(float scale)
        {
            _globalGravityScale = Mathf.Max(0f, scale);
            if (_currentLevelData != null)
            {
                _globalGravity = LevelData.GetGravityVectorFromDirection(
                    _currentLevelData.globalGravityDirection) * _globalGravityScale;
                Physics2D.gravity = _globalGravity;
                SyncAllRigidbodyGravity();
            }
        }

        public void SetGlobalGravityDirection(GravityDirection direction)
        {
            if (_currentLevelData != null)
            {
                _currentLevelData.globalGravityDirection = direction;
                _globalGravity = _currentLevelData.GetGravityVector();
                Physics2D.gravity = _globalGravity;
                SyncAllRigidbodyGravity();
            }
        }

        public void EnableGravity()
        {
            _isGravityEnabled = true;
            Physics2D.gravity = _globalGravity;
            SyncAllRigidbodyGravity();
        }

        public void DisableGravity()
        {
            _isGravityEnabled = false;
            Physics2D.gravity = Vector2.zero;
            SyncAllRigidbodyGravity();
        }

        private void SyncAllRigidbodyGravity()
        {
            Rigidbody2D[] allRigidbodies = FindObjectsOfType<Rigidbody2D>();
            foreach (var rb in allRigidbodies)
            {
                if (rb.bodyType == RigidbodyType2D.Dynamic)
                {
                    bool inZone = false;
                    foreach (var zone in _activeGravityZones)
                    {
                        if (zone.IsPositionInZone(rb.position))
                        {
                            rb.gravityScale = 0f;
                            inZone = true;
                            break;
                        }
                    }

                    if (!inZone)
                    {
                        rb.gravityScale = _isGravityEnabled ? 1f : 0f;
                    }
                }
            }
        }

        public void ToggleGravity()
        {
            if (_isGravityEnabled)
                DisableGravity();
            else
                EnableGravity();
        }

        public Vector2 GetGravityAtPosition(Vector2 position)
        {
            if (!_isGravityEnabled)
                return Vector2.zero;

            foreach (var zone in _activeGravityZones)
            {
                if (zone.IsPositionInZone(position))
                {
                    return zone.GetZoneGravity();
                }
            }

            return _globalGravity;
        }

        public void ApplyGravityToRigidbody(Rigidbody2D rb)
        {
            if (rb == null || !_isGravityEnabled) return;

            Vector2 gravity = GetGravityAtPosition(rb.position);
            rb.AddForce(gravity * rb.mass, ForceMode2D.Force);
        }

        public void RegisterRigidbody(Rigidbody2D rb)
        {
            if (!_affectedRigidbodies.Contains(rb))
                _affectedRigidbodies.Add(rb);
        }

        public void UnregisterRigidbody(Rigidbody2D rb)
        {
            _affectedRigidbodies.Remove(rb);
        }

        private void FixedUpdate()
        {
            if (!_isGravityEnabled) return;

            for (int i = _affectedRigidbodies.Count - 1; i >= 0; i--)
            {
                var rb = _affectedRigidbodies[i];
                if (rb == null)
                {
                    _affectedRigidbodies.RemoveAt(i);
                    continue;
                }

                if (rb.bodyType == RigidbodyType2D.Dynamic)
                {
                    Vector2 gravity = GetGravityAtPosition(rb.position);
                    rb.AddForce(gravity * rb.mass, ForceMode2D.Force);
                }
            }
        }

        public void Cleanup()
        {
            ClearGravityZones();
            _affectedRigidbodies.Clear();
            _currentLevelData = null;
        }

        public List<GravityZone> GetActiveGravityZones()
        {
            return _activeGravityZones;
        }

        public bool IsInGravityZone(Vector2 position)
        {
            foreach (var zone in _activeGravityZones)
            {
                if (zone.IsPositionInZone(position))
                    return true;
            }
            return false;
        }

        public GravityZone GetGravityZoneAtPosition(Vector2 position)
        {
            foreach (var zone in _activeGravityZones)
            {
                if (zone.IsPositionInZone(position))
                    return zone;
            }
            return null;
        }
    }

    public class GravityZone : MonoBehaviour
    {
        private GravityZoneData _zoneData;
        private BoxCollider2D _zoneCollider;
        private Vector2 _gravityForce;

        public Vector2 GravityForce => _gravityForce;
        public GravityZoneData ZoneData => _zoneData;

        public void Initialize(GravityZoneData data)
        {
            _zoneData = data;

            _zoneCollider = gameObject.AddComponent<BoxCollider2D>();
            _zoneCollider.isTrigger = true;
            _zoneCollider.size = Vector2.one;

            _gravityForce = LevelData.GetGravityVectorFromDirection(data.gravityDirection) * data.gravityScale;

            SpriteRenderer renderer = gameObject.AddComponent<SpriteRenderer>();
            renderer.color = new Color(0.5f, 0.5f, 1f, 0.2f);
            renderer.sortingOrder = -100;

            Texture2D tex = new Texture2D(1, 1);
            tex.SetPixel(0, 0, new Color(0.5f, 0.5f, 1f, 0.2f));
            tex.Apply();
            renderer.sprite = Sprite.Create(tex, new Rect(0, 0, 1, 1), new Vector2(0.5f, 0.5f));
            transform.localScale = new Vector3(data.size.x, data.size.y, 1);
        }

        public bool IsPositionInZone(Vector2 position)
        {
            if (_zoneCollider == null) return false;
            return _zoneCollider.OverlapPoint(position);
        }

        public Vector2 GetZoneGravity()
        {
            return _gravityForce;
        }

        public void OnTriggerEnter2D(Collider2D other)
        {
            Rigidbody2D rb = other.attachedRigidbody;
            if (rb != null && rb.bodyType == RigidbodyType2D.Dynamic)
            {
                rb.gravityScale = 0f;
            }
        }

        public void OnTriggerExit2D(Collider2D other)
        {
            Rigidbody2D rb = other.attachedRigidbody;
            if (rb != null && rb.bodyType == RigidbodyType2D.Dynamic)
            {
                rb.gravityScale = 1f;
            }
        }

        private void OnTriggerStay2D(Collider2D other)
        {
            Rigidbody2D rb = other.attachedRigidbody;
            if (rb != null && rb.bodyType == RigidbodyType2D.Dynamic)
            {
                rb.AddForce(_gravityForce * rb.mass, ForceMode2D.Force);
            }
        }
    }
}
