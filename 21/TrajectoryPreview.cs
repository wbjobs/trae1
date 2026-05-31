using System;
using System.Collections.Generic;
using UnityEngine;

namespace PhysicsPuzzleGame
{
    public class TrajectoryPreview : MonoBehaviour
    {
        private static TrajectoryPreview _instance;
        public static TrajectoryPreview Instance
        {
            get
            {
                if (_instance == null)
                {
                    GameObject go = new GameObject("TrajectoryPreview");
                    _instance = go.AddComponent<TrajectoryPreview>();
                    DontDestroyOnLoad(go);
                }
                return _instance;
            }
        }

        [Header("预览设置")]
        public int maxPredictionSteps = 100;
        public float timeStep = 0.02f;
        public float maxPreviewDistance = 30f;

        [Header("视觉设置")]
        public Color trajectoryColor = new Color(1f, 1f, 0f, 0.8f);
        public Color collisionColor = new Color(1f, 0f, 0f, 0.8f);
        public float pointSize = 0.15f;
        public float lineWidth = 0.05f;

        private LineRenderer _trajectoryLine;
        private LineRenderer _collisionLine;
        private GameObject _previewContainer;
        private bool _isPreviewEnabled = true;
        private bool _isInitialized = false;

        private List<Vector2> _trajectoryPoints = new List<Vector2>();
        private Vector2 _collisionPoint;
        private bool _hasCollision;

        public bool IsPreviewEnabled => _isPreviewEnabled;
        public List<Vector2> TrajectoryPoints => _trajectoryPoints;
        public bool HasCollision => _hasCollision;
        public Vector2 CollisionPoint => _collisionPoint;

        public event Action<List<Vector2>> OnTrajectoryUpdated;
        public event Action<Vector2> OnCollisionDetected;

        public void Initialize()
        {
            if (_isInitialized) return;

            _previewContainer = new GameObject("TrajectoryPreviewContainer");
            _previewContainer.transform.SetParent(transform);

            _trajectoryLine = CreateLineRenderer("TrajectoryLine", trajectoryColor);
            _collisionLine = CreateLineRenderer("CollisionLine", collisionColor);

            _trajectoryLine.transform.SetParent(_previewContainer.transform);
            _collisionLine.transform.SetParent(_previewContainer.transform);

            _isInitialized = true;
            HidePreview();
        }

        private LineRenderer CreateLineRenderer(string name, Color color)
        {
            GameObject lineObj = new GameObject(name);
            LineRenderer line = lineObj.AddComponent<LineRenderer>();
            line.material = new Material(Shader.Find("Sprites/Default"));
            line.startColor = color;
            line.endColor = color;
            line.startWidth = lineWidth;
            line.endWidth = lineWidth;
            line.sortingOrder = 100;
            line.loop = false;
            line.positionCount = 0;
            return line;
        }

        public void ShowPreview()
        {
            if (_previewContainer != null)
            {
                _previewContainer.SetActive(true);
            }
            _isPreviewEnabled = true;
        }

        public void HidePreview()
        {
            if (_previewContainer != null)
            {
                _previewContainer.SetActive(false);
            }
            _isPreviewEnabled = false;
        }

        public void TogglePreview()
        {
            if (_isPreviewEnabled)
                HidePreview();
            else
                ShowPreview();
        }

        public void PredictTrajectory(Vector2 startPosition, Vector2 velocity, LayerMask collisionMask)
        {
            if (!_isPreviewEnabled || !_isInitialized) return;

            _trajectoryPoints.Clear();
            _hasCollision = false;

            Vector2 currentPosition = startPosition;
            Vector2 currentVelocity = velocity;

            _trajectoryPoints.Add(currentPosition);

            Vector2 gravity = GravitySimulator.Instance != null
                ? GravitySimulator.Instance.GetGravityAtPosition(startPosition)
                : Physics2D.gravity;

            for (int i = 0; i < maxPredictionSteps; i++)
            {
                Vector2 gravityAtPosition = GravitySimulator.Instance != null
                    ? GravitySimulator.Instance.GetGravityAtPosition(currentPosition)
                    : gravity;

                currentVelocity += gravityAtPosition * timeStep;
                currentPosition += currentVelocity * timeStep;

                float distance = Vector2.Distance(startPosition, currentPosition);
                if (distance > maxPreviewDistance)
                    break;

                bool hitCollision = CheckCollision(currentPosition, collisionMask);
                if (hitCollision)
                {
                    _hasCollision = true;
                    _collisionPoint = currentPosition;
                    _trajectoryPoints.Add(currentPosition);
                    OnCollisionDetected?.Invoke(_collisionPoint);
                    break;
                }

                _trajectoryPoints.Add(currentPosition);
            }

            UpdateLineRenderers();
            OnTrajectoryUpdated?.Invoke(_trajectoryPoints);
        }

        private bool CheckCollision(Vector2 position, LayerMask collisionMask)
        {
            float collisionCheckRadius = 0.3f;
            Collider2D hit = Physics2D.OverlapCircle(position, collisionCheckRadius, collisionMask);
            return hit != null;
        }

        public void PredictJumpTrajectory(Vector2 startPosition, float jumpForce, LayerMask collisionMask)
        {
            float gravityScale = GravitySimulator.Instance != null
                ? GravitySimulator.Instance.GlobalGravityScale
                : 1f;

            Vector2 gravity = GravitySimulator.Instance != null
                ? GravitySimulator.Instance.GetGravityAtPosition(startPosition)
                : Physics2D.gravity;

            gravity *= gravityScale;

            Vector2 velocity = new Vector2(0, jumpForce);
            PredictTrajectory(startPosition, velocity, collisionMask);
        }

        public void PredictProjectileTrajectory(Vector2 startPosition, Vector2 direction, float force, LayerMask collisionMask)
        {
            Vector2 velocity = direction.normalized * force;
            PredictTrajectory(startPosition, velocity, collisionMask);
        }

        private void UpdateLineRenderers()
        {
            if (_trajectoryLine == null || _collisionLine == null) return;

            if (_trajectoryPoints.Count < 2)
            {
                _trajectoryLine.positionCount = 0;
                _collisionLine.positionCount = 0;
                return;
            }

            _trajectoryLine.positionCount = _trajectoryPoints.Count;
            for (int i = 0; i < _trajectoryPoints.Count; i++)
            {
                _trajectoryLine.SetPosition(i, new Vector3(_trajectoryPoints[i].x, _trajectoryPoints[i].y, 0));
            }

            if (_hasCollision)
            {
                _collisionLine.positionCount = 2;
                _collisionLine.SetPosition(0, new Vector3(_collisionPoint.x - 0.5f, _collisionPoint.y, 0));
                _collisionLine.SetPosition(1, new Vector3(_collisionPoint.x + 0.5f, _collisionPoint.y, 0));
            }
            else
            {
                _collisionLine.positionCount = 0;
            }
        }

        public float CalculateJumpHeight(float jumpForce)
        {
            Vector2 gravity = GravitySimulator.Instance != null
                ? GravitySimulator.Instance.GlobalGravity
                : Physics2D.gravity;

            float gravityMagnitude = gravity.magnitude;
            if (gravityMagnitude < 0.001f) return 100f;

            return (jumpForce * jumpForce) / (2f * gravityMagnitude);
        }

        public float CalculateJumpDistance(float jumpForce, float horizontalSpeed)
        {
            Vector2 gravity = GravitySimulator.Instance != null
                ? GravitySimulator.Instance.GlobalGravity
                : Physics2D.gravity;

            float gravityMagnitude = gravity.magnitude;
            if (gravityMagnitude < 0.001f) return 100f;

            float flightTime = (2f * jumpForce) / gravityMagnitude;
            return horizontalSpeed * flightTime;
        }

        public List<Vector2> GetLandingSpots(Vector2 startPosition, float minJumpForce, float maxJumpForce, int samples, LayerMask collisionMask)
        {
            List<Vector2> landingSpots = new List<Vector2>();
            float forceStep = (maxJumpForce - minJumpForce) / Mathf.Max(1, samples - 1);

            for (int i = 0; i < samples; i++)
            {
                float force = minJumpForce + forceStep * i;
                Vector2 landingSpot = PredictLandingPosition(startPosition, force, collisionMask);
                if (landingSpot != Vector2.zero)
                {
                    landingSpots.Add(landingSpot);
                }
            }

            return landingSpots;
        }

        private Vector2 PredictLandingPosition(Vector2 startPosition, float jumpForce, LayerMask collisionMask)
        {
            Vector2 currentPosition = startPosition;
            Vector2 currentVelocity = new Vector2(0, jumpForce);

            Vector2 gravity = GravitySimulator.Instance != null
                ? GravitySimulator.Instance.GetGravityAtPosition(startPosition)
                : Physics2D.gravity;

            for (int i = 0; i < maxPredictionSteps; i++)
            {
                Vector2 gravityAtPosition = GravitySimulator.Instance != null
                    ? GravitySimulator.Instance.GetGravityAtPosition(currentPosition)
                    : gravity;

                currentVelocity += gravityAtPosition * timeStep;
                currentPosition += currentVelocity * timeStep;

                if (CheckCollision(currentPosition, collisionMask))
                {
                    return currentPosition;
                }

                if (currentVelocity.y < 0 && CheckCollision(currentPosition, collisionMask))
                {
                    return currentPosition;
                }
            }

            return Vector2.zero;
        }

        public void SetTrajectoryColor(Color color)
        {
            trajectoryColor = color;
            if (_trajectoryLine != null)
            {
                _trajectoryLine.startColor = color;
                _trajectoryLine.endColor = color;
            }
        }

        public void SetTrajectoryOpacity(float opacity)
        {
            if (_trajectoryLine != null)
            {
                Color color = _trajectoryLine.startColor;
                color.a = opacity;
                _trajectoryLine.startColor = color;
                _trajectoryLine.endColor = color;
            }
        }

        public void UpdatePreviewPosition(Vector2 position, Vector2 velocity, LayerMask collisionMask)
        {
            if (!_isPreviewEnabled) return;

            if (velocity.magnitude > 0.1f)
            {
                PredictTrajectory(position, velocity, collisionMask);
                ShowPreview();
            }
            else
            {
                HidePreview();
            }
        }

        public void ClearPreview()
        {
            _trajectoryPoints.Clear();
            _hasCollision = false;

            if (_trajectoryLine != null)
                _trajectoryLine.positionCount = 0;

            if (_collisionLine != null)
                _collisionLine.positionCount = 0;
        }

        public void Cleanup()
        {
            ClearPreview();

            if (_previewContainer != null)
            {
                Destroy(_previewContainer);
            }

            _isInitialized = false;
        }

        private void OnDestroy()
        {
            Cleanup();
        }
    }

    public class TrajectoryIndicator : MonoBehaviour
    {
        private GameObject _indicator;
        private SpriteRenderer _renderer;
        private bool _isVisible;

        public void Initialize()
        {
            _indicator = new GameObject("TrajectoryIndicator");
            _indicator.transform.SetParent(transform);

            _renderer = _indicator.AddComponent<SpriteRenderer>();
            Texture2D tex = new Texture2D(16, 16);
            for (int y = 0; y < 16; y++)
            {
                for (int x = 0; x < 16; x++)
                {
                    float dist = Vector2.Distance(new Vector2(x, y), new Vector2(8, 8));
                    if (dist < 7)
                    {
                        tex.SetPixel(x, y, Color.white);
                    }
                    else if (dist < 8)
                    {
                        tex.SetPixel(x, y, new Color(1f, 1f, 1f, 0.5f));
                    }
                    else
                    {
                        tex.SetPixel(x, y, Color.clear);
                    }
                }
            }
            tex.Apply();
            _renderer.sprite = Sprite.Create(tex, new Rect(0, 0, 16, 16), new Vector2(0.5f, 0.5f));
            _renderer.color = new Color(1f, 1f, 0f, 0.8f);
            _renderer.sortingOrder = 150;
            _renderer.enabled = false;
        }

        public void Show(Vector2 position)
        {
            if (_indicator == null) Initialize();
            _indicator.transform.position = position;
            _renderer.enabled = true;
            _isVisible = true;
        }

        public void Hide()
        {
            if (_renderer != null)
            {
                _renderer.enabled = false;
            }
            _isVisible = false;
        }

        public void SetColor(Color color)
        {
            if (_renderer != null)
            {
                _renderer.color = color;
            }
        }

        public void SetSize(float size)
        {
            if (_indicator != null)
            {
                _indicator.transform.localScale = Vector3.one * size;
            }
        }

        public bool IsVisible => _isVisible;
    }
}
