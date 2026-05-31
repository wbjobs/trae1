using System;
using System.Collections.Generic;
using UnityEngine;

namespace PhysicsPuzzleGame
{
    public enum PhysicsLayer
    {
        Player = 6,
        Platform = 7,
        Obstacle = 8,
        Item = 9,
        Hazard = 10,
        Goal = 11,
        MovingPlatform = 12
    }

    public enum CollisionEventType
    {
        OnCollisionEnter,
        OnCollisionExit,
        OnCollisionStay,
        OnTriggerEnter,
        OnTriggerExit,
        OnTriggerStay
    }

    public class PhysicsCollision2D : MonoBehaviour
    {
        private static PhysicsCollision2D _instance;
        public static PhysicsCollision2D Instance
        {
            get
            {
                if (_instance == null)
                {
                    GameObject go = new GameObject("PhysicsCollision2D");
                    _instance = go.AddComponent<PhysicsCollision2D>();
                    DontDestroyOnLoad(go);
                }
                return _instance;
            }
        }

        private List<PhysicsMaterial> _customMaterials = new List<PhysicsMaterial>();
        private Dictionary<int, PhysicsMaterial> _layerMaterials = new Dictionary<int, PhysicsMaterial>();
        private bool _collisionEnabled = true;

        public bool CollisionEnabled => _collisionEnabled;

        public class CollisionInfo
        {
            public GameObject source;
            public GameObject target;
            public Vector2 contactPoint;
            public Vector2 normal;
            public float relativeVelocity;
            public CollisionEventType eventType;
            public float impulse;
        }

        public delegate void CollisionHandler(CollisionInfo info);

        private Dictionary<int, Dictionary<CollisionEventType, List<CollisionHandler>>> _collisionHandlers
            = new Dictionary<int, Dictionary<CollisionEventType, List<CollisionHandler>>>();

        public void RegisterCollisionHandler(int layer, CollisionEventType eventType, CollisionHandler handler)
        {
            if (!_collisionHandlers.ContainsKey(layer))
            {
                _collisionHandlers[layer] = new Dictionary<CollisionEventType, List<CollisionHandler>>();
            }

            if (!_collisionHandlers[layer].ContainsKey(eventType))
            {
                _collisionHandlers[layer][eventType] = new List<CollisionHandler>();
            }

            _collisionHandlers[layer][eventType].Add(handler);
        }

        public void UnregisterCollisionHandler(int layer, CollisionEventType eventType, CollisionHandler handler)
        {
            if (_collisionHandlers.ContainsKey(layer) &&
                _collisionHandlers[layer].ContainsKey(eventType))
            {
                _collisionHandlers[layer][eventType].Remove(handler);
            }
        }

        public void FireCollisionEvent(int layer, CollisionEventType eventType, CollisionInfo info)
        {
            if (!_collisionEnabled) return;

            if (_collisionHandlers.ContainsKey(layer) &&
                _collisionHandlers[layer].ContainsKey(eventType))
            {
                foreach (var handler in _collisionHandlers[layer][eventType])
                {
                    handler?.Invoke(info);
                }
            }
        }

        public void EnableCollision()
        {
            _collisionEnabled = true;
        }

        public void DisableCollision()
        {
            _collisionEnabled = false;
        }

        public void SetLayerCollisionMatrix(int layer1, int layer2, bool enabled)
        {
            Physics2D.IgnoreLayerCollision(layer1, layer2, !enabled);
        }

        public bool CheckCollision(Vector2 position, Vector2 size, LayerMask layerMask)
        {
            Collider2D hit = Physics2D.OverlapBox(position, size, 0f, layerMask);
            return hit != null;
        }

        public RaycastHit2D CastRay(Vector2 origin, Vector2 direction, float distance, LayerMask layerMask)
        {
            return Physics2D.Raycast(origin, direction, distance, layerMask);
        }

        public RaycastHit2D[] CastRayAll(Vector2 origin, Vector2 direction, float distance, LayerMask layerMask)
        {
            return Physics2D.RaycastAll(origin, direction, distance, layerMask);
        }

        public Collider2D[] OverlapArea(Vector2 lowerLeft, Vector2 upperRight, LayerMask layerMask)
        {
            return Physics2D.OverlapAreaAll(lowerLeft, upperRight, layerMask);
        }

        public Collider2D[] OverlapCircle(Vector2 center, float radius, LayerMask layerMask)
        {
            return Physics2D.OverlapCircleAll(center, radius, layerMask);
        }

        public PhysicsMaterial2D CreatePhysicsMaterial(string name, float friction, float bounciness)
        {
            PhysicsMaterial2D material = new PhysicsMaterial2D(name);
            material.friction = friction;
            material.bounciness = bounciness;
            return material;
        }

        public void ApplyMaterialToCollider(Collider2D collider, PhysicsMaterial2D material)
        {
            if (collider != null)
            {
                collider.sharedMaterial = material;
            }
        }

        public bool IsGrounded(Rigidbody2D rb, float groundCheckDistance = 0.1f)
        {
            if (rb == null) return false;

            Collider2D collider = rb.GetComponent<Collider2D>();
            if (collider == null) return false;

            Bounds bounds = collider.bounds;
            Vector2 origin = new Vector2(bounds.center.x, bounds.min.y - 0.01f);
            Vector2 size = new Vector2(bounds.size.x * 0.9f, groundCheckDistance);

            RaycastHit2D hit = Physics2D.BoxCast(
                origin, size, 0f, Vector2.down, groundCheckDistance,
                LayerMask.GetMask("Platform", "MovingPlatform"));

            return hit.collider != null;
        }

        public bool IsTouchingWall(Rigidbody2D rb, float wallCheckDistance = 0.1f)
        {
            if (rb == null) return false;

            Collider2D collider = rb.GetComponent<Collider2D>();
            if (collider == null) return false;

            Bounds bounds = collider.bounds;
            Vector2 origin = new Vector2(bounds.max.x + 0.01f, bounds.center.y);
            Vector2 size = new Vector2(wallCheckDistance, bounds.size.y * 0.9f);

            RaycastHit2D hitRight = Physics2D.BoxCast(
                origin, size, 0f, Vector2.right, wallCheckDistance,
                LayerMask.GetMask("Platform", "MovingPlatform"));

            origin = new Vector2(bounds.min.x - 0.01f, bounds.center.y);
            RaycastHit2D hitLeft = Physics2D.BoxCast(
                origin, size, 0f, Vector2.left, wallCheckDistance,
                LayerMask.GetMask("Platform", "MovingPlatform"));

            return hitRight.collider != null || hitLeft.collider != null;
        }

        public bool IsCeilinged(Rigidbody2D rb, float ceilingCheckDistance = 0.1f)
        {
            if (rb == null) return false;

            Collider2D collider = rb.GetComponent<Collider2D>();
            if (collider == null) return false;

            Bounds bounds = collider.bounds;
            Vector2 origin = new Vector2(bounds.center.x, bounds.max.y + 0.01f);
            Vector2 size = new Vector2(bounds.size.x * 0.9f, ceilingCheckDistance);

            RaycastHit2D hit = Physics2D.BoxCast(
                origin, size, 0f, Vector2.up, ceilingCheckDistance,
                LayerMask.GetMask("Platform", "MovingPlatform"));

            return hit.collider != null;
        }

        public Vector2 CalculateCollisionResponse(Vector2 incomingVelocity, Vector2 normal, float bounciness)
        {
            float dotProduct = Vector2.Dot(incomingVelocity, normal);
            return incomingVelocity - 2f * dotProduct * normal * bounciness;
        }

        public float CalculateImpulse(Rigidbody2D rb1, Rigidbody2D rb2, Vector2 normal)
        {
            if (rb1 == null || rb2 == null) return 0f;

            float e = 0.5f;
            float m1 = rb1.mass;
            float m2 = rb2.mass;
            float v1 = Vector2.Dot(rb1.velocity, normal);
            float v2 = Vector2.Dot(rb2.velocity, normal);

            float impulse = -(1f + e) * (v1 - v2) / (1f / m1 + 1f / m2);
            return Mathf.Abs(impulse);
        }

        private Dictionary<Rigidbody2D, CollisionCache> _collisionCache = new Dictionary<Rigidbody2D, CollisionCache>();

        private class CollisionCache
        {
            public Vector2 lastPosition;
            public Vector2 predictedPosition;
            public float cacheTime;
            public const float CacheDuration = 0.1f;
        }

        public bool PredictCollision(Vector2 startPosition, Vector2 velocity, float distance, LayerMask layerMask)
        {
            Vector2 endPosition = startPosition + velocity.normalized * distance;
            return Physics2D.Linecast(startPosition, endPosition, layerMask);
        }

        public Vector2 CalculateSafePosition(Vector2 currentPosition, Vector2 targetPosition, LayerMask layerMask, float skinWidth = 0.05f)
        {
            Vector2 direction = targetPosition - currentPosition;
            float distance = direction.magnitude;

            if (distance < 0.001f) return currentPosition;

            direction.Normalize();

            RaycastHit2D hit = Physics2D.Raycast(currentPosition, direction, distance, layerMask);

            if (hit.collider != null)
            {
                float hitDistance = hit.distance - skinWidth;
                if (hitDistance < 0) hitDistance = 0;
                return currentPosition + direction * hitDistance;
            }

            return targetPosition;
        }

        public List<ContactPoint2D> GetAllContacts(Rigidbody2D rb)
        {
            List<ContactPoint2D> contacts = new List<ContactPoint2D>();
            if (rb != null)
            {
                ContactPoint2D[] tempContacts = new ContactPoint2D[10];
                int contactCount = rb.GetContacts(tempContacts);
                for (int i = 0; i < contactCount; i++)
                {
                    contacts.Add(tempContacts[i]);
                }
            }
            return contacts;
        }

        public Vector2 CalculateCollisionResponse(Vector2 incomingVelocity, Vector2 normal, float bounciness, float friction)
        {
            Vector2 perpendicular = new Vector2(normal.y, -normal.x);

            float vn = Vector2.Dot(incomingVelocity, normal);
            float vt = Vector2.Dot(incomingVelocity, perpendicular);

            Vector2 reflected = -vn * bounciness * normal;
            Vector2 tangent = vt * (1f - friction) * perpendicular;

            return reflected + tangent;
        }

        public bool IsCollidingWithLayer(GameObject obj, int layer)
        {
            Collider2D[] colliders = obj.GetComponents<Collider2D>();
            foreach (var collider in colliders)
            {
                if (collider.IsTouchingLayers(1 << layer))
                    return true;
            }
            return false;
        }

        public Collider2D[] GetOverlappingColliders(Collider2D collider, LayerMask layerMask)
        {
            if (collider == null) return new Collider2D[0];

            ContactFilter2D filter = new ContactFilter2D();
            filter.SetLayerMask(layerMask);
            filter.useTriggers = true;

            List<Collider2D> results = new List<Collider2D>();
            collider.OverlapCollider(filter, results);
            return results.ToArray();
        }

        public Vector2 SweepTest(Rigidbody2D rb, Vector2 direction, float distance, LayerMask layerMask)
        {
            if (rb == null) return Vector2.zero;

            RaycastHit2D hit = rb.Cast(direction, new ContactFilter2D().NoFilter(), null, distance);
            if (hit.collider != null)
            {
                return hit.point;
            }
            return rb.position + direction * distance;
        }

        public void IgnoreCollision(GameObject obj1, GameObject obj2, bool ignore)
        {
            Collider2D[] colliders1 = obj1.GetComponents<Collider2D>();
            Collider2D[] colliders2 = obj2.GetComponents<Collider2D>();

            foreach (var c1 in colliders1)
            {
                foreach (var c2 in colliders2)
                {
                    Physics2D.IgnoreCollision(c1, c2, ignore);
                }
            }
        }

        public void Cleanup()
        {
            _collisionHandlers.Clear();
            _customMaterials.Clear();
            _layerMaterials.Clear();
        }
    }

    public class CollisionDetector : MonoBehaviour
    {
        private PhysicsCollision2D _collisionSystem;
        private Collider2D _collider;
        private Rigidbody2D _rigidbody;

        public Action<Collision2D> OnCollisionEnter2DEvent;
        public Action<Collision2D> OnCollisionExit2DEvent;
        public Action<Collision2D> OnCollisionStay2DEvent;
        public Action<Collider2D> OnTriggerEnter2DEvent;
        public Action<Collider2D> OnTriggerExit2DEvent;
        public Action<Collider2D> OnTriggerStay2DEvent;

        private void Awake()
        {
            _collisionSystem = PhysicsCollision2D.Instance;
            _collider = GetComponent<Collider2D>();
            _rigidbody = GetComponent<Rigidbody2D>();
        }

        private void OnCollisionEnter2D(Collision2D collision)
        {
            OnCollisionEnter2DEvent?.Invoke(collision);

            if (_collisionSystem != null)
            {
                PhysicsCollision2D.CollisionInfo info = new PhysicsCollision2D.CollisionInfo
                {
                    source = gameObject,
                    target = collision.gameObject,
                    contactPoint = collision.GetContact(0).point,
                    normal = collision.GetContact(0).normal,
                    relativeVelocity = collision.relativeVelocity.magnitude,
                    eventType = CollisionEventType.OnCollisionEnter,
                    impulse = collision.impulse.magnitude
                };

                _collisionSystem.FireCollisionEvent(gameObject.layer, CollisionEventType.OnCollisionEnter, info);
            }
        }

        private void OnCollisionExit2D(Collision2D collision)
        {
            OnCollisionExit2DEvent?.Invoke(collision);

            if (_collisionSystem != null)
            {
                PhysicsCollision2D.CollisionInfo info = new PhysicsCollision2D.CollisionInfo
                {
                    source = gameObject,
                    target = collision.gameObject,
                    contactPoint = collision.GetContact(0).point,
                    normal = collision.GetContact(0).normal,
                    relativeVelocity = collision.relativeVelocity.magnitude,
                    eventType = CollisionEventType.OnCollisionExit,
                    impulse = collision.impulse.magnitude
                };

                _collisionSystem.FireCollisionEvent(gameObject.layer, CollisionEventType.OnCollisionExit, info);
            }
        }

        private void OnCollisionStay2D(Collision2D collision)
        {
            OnCollisionStay2DEvent?.Invoke(collision);
        }

        private void OnTriggerEnter2D(Collider2D other)
        {
            OnTriggerEnter2DEvent?.Invoke(other);

            if (_collisionSystem != null)
            {
                PhysicsCollision2D.CollisionInfo info = new PhysicsCollision2D.CollisionInfo
                {
                    source = gameObject,
                    target = other.gameObject,
                    contactPoint = transform.position,
                    normal = (other.transform.position - transform.position).normalized,
                    relativeVelocity = _rigidbody != null ? _rigidbody.velocity.magnitude : 0f,
                    eventType = CollisionEventType.OnTriggerEnter,
                    impulse = 0f
                };

                _collisionSystem.FireCollisionEvent(gameObject.layer, CollisionEventType.OnTriggerEnter, info);
            }
        }

        private void OnTriggerExit2D(Collider2D other)
        {
            OnTriggerExit2DEvent?.Invoke(other);

            if (_collisionSystem != null)
            {
                PhysicsCollision2D.CollisionInfo info = new PhysicsCollision2D.CollisionInfo
                {
                    source = gameObject,
                    target = other.gameObject,
                    contactPoint = transform.position,
                    normal = (other.transform.position - transform.position).normalized,
                    relativeVelocity = 0f,
                    eventType = CollisionEventType.OnTriggerExit,
                    impulse = 0f
                };

                _collisionSystem.FireCollisionEvent(gameObject.layer, CollisionEventType.OnTriggerExit, info);
            }
        }

        private void OnTriggerStay2D(Collider2D other)
        {
            OnTriggerStay2DEvent?.Invoke(other);
        }
    }
}
