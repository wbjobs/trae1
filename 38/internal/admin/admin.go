package admin

import (
	"context"
	"crypto/x509"
	"net/http"
	"time"

	"github.com/gin-contrib/cors"
	"github.com/gin-gonic/gin"

	"github.com/spiffe-gateway/svid-gateway/internal/audit"
	"github.com/spiffe-gateway/svid-gateway/internal/identity"
	"github.com/spiffe-gateway/svid-gateway/internal/policy"
	"github.com/spiffe-gateway/svid-gateway/internal/registry"
)

type Store interface {
	List(ctx context.Context) ([]*policy.Policy, error)
	Save(ctx context.Context, p *policy.Policy) error
	Delete(ctx context.Context, id string) error
	ListAudit(ctx context.Context, limit int64) ([][]byte, error)
}

type Server struct {
	store       Store
	engine      *policy.Engine
	identity    identity.Provider
	registry    *registry.Registry
	auditLogger *audit.Logger
	trustRoots  *x509.CertPool
}

func New(s Store, e *policy.Engine, p identity.Provider, r *registry.Registry, a *audit.Logger, roots *x509.CertPool) *Server {
	return &Server{store: s, engine: e, identity: p, registry: r, auditLogger: a, trustRoots: roots}
}

func (s *Server) Router() *gin.Engine {
	g := gin.New()
	g.Use(gin.Recovery(), gin.Logger())
	g.Use(cors.New(cors.Config{
		AllowAllOrigins:  true,
		AllowMethods:     []string{"GET", "POST", "PUT", "DELETE", "OPTIONS"},
		AllowHeaders:     []string{"Authorization", "Content-Type"},
		AllowCredentials: false,
	}))

	api := g.Group("/api/v1")
	{
		api.GET("/health", func(c *gin.Context) { c.JSON(http.StatusOK, gin.H{"status": "ok"}) })

		api.GET("/policies", s.listPolicies)
		api.POST("/policies", s.createPolicy)
		api.PUT("/policies/:id", s.updatePolicy)
		api.DELETE("/policies/:id", s.deletePolicy)
		api.POST("/policies/test", s.testPolicy)

		api.GET("/identities", s.listIdentities)
		api.POST("/identities", s.registerIdentity)
		api.DELETE("/identities/*id", s.deleteIdentity)

		api.GET("/svids", s.listSVIDs)
		api.GET("/svids/jwt", s.getJWT)

		api.GET("/status/degrade", s.getDegradeStatus)

		api.GET("/audit", s.listAudit)
	}

	g.StaticFS("/ui", http.Dir("./web/build"))
	g.GET("/", func(c *gin.Context) { c.File("./web/build/index.html") })

	return g
}

func (s *Server) listPolicies(c *gin.Context) {
	list, err := s.store.List(c.Request.Context())
	if err != nil {
		c.JSON(http.StatusInternalServerError, gin.H{"error": err.Error()})
		return
	}
	c.JSON(http.StatusOK, list)
}

func (s *Server) createPolicy(c *gin.Context) {
	var p policy.Policy
	if err := c.ShouldBindJSON(&p); err != nil {
		c.JSON(http.StatusBadRequest, gin.H{"error": err.Error()})
		return
	}
	if p.ID == "" {
		p.ID = "pol_" + randomHex(8)
	}
	p.CreatedAt = time.Now().UTC()
	p.UpdatedAt = p.CreatedAt
	if err := policy.Validate(&p); err != nil {
		c.JSON(http.StatusBadRequest, gin.H{"error": err.Error()})
		return
	}
	if err := s.store.Save(c.Request.Context(), &p); err != nil {
		c.JSON(http.StatusInternalServerError, gin.H{"error": err.Error()})
		return
	}
	s.auditLogger.Log(c.Request.Context(), audit.Entry{
		Action:   "policy.create",
		Operator: c.ClientIP(),
		PolicyID: p.ID,
		After:    &p,
	})
	c.JSON(http.StatusCreated, p)
}

func (s *Server) updatePolicy(c *gin.Context) {
	id := c.Param("id")
	list, _ := s.store.List(c.Request.Context())
	var before *policy.Policy
	for _, p := range list {
		if p.ID == id {
			before = p
			break
		}
	}
	var p policy.Policy
	if err := c.ShouldBindJSON(&p); err != nil {
		c.JSON(http.StatusBadRequest, gin.H{"error": err.Error()})
		return
	}
	p.ID = id
	p.UpdatedAt = time.Now().UTC()
	if err := policy.Validate(&p); err != nil {
		c.JSON(http.StatusBadRequest, gin.H{"error": err.Error()})
		return
	}
	if err := s.store.Save(c.Request.Context(), &p); err != nil {
		c.JSON(http.StatusInternalServerError, gin.H{"error": err.Error()})
		return
	}
	s.auditLogger.Log(c.Request.Context(), audit.Entry{
		Action:   "policy.update",
		Operator: c.ClientIP(),
		PolicyID: p.ID,
		Before:   before,
		After:    &p,
	})
	c.JSON(http.StatusOK, p)
}

func (s *Server) deletePolicy(c *gin.Context) {
	id := c.Param("id")
	list, _ := s.store.List(c.Request.Context())
	var before *policy.Policy
	for _, p := range list {
		if p.ID == id {
			before = p
			break
		}
	}
	if err := s.store.Delete(c.Request.Context(), id); err != nil {
		c.JSON(http.StatusInternalServerError, gin.H{"error": err.Error()})
		return
	}
	s.auditLogger.Log(c.Request.Context(), audit.Entry{
		Action:   "policy.delete",
		Operator: c.ClientIP(),
		PolicyID: id,
		Before:   before,
	})
	c.Status(http.StatusNoContent)
}

type testRequest struct {
	Source      string `json:"source"`
	Destination string `json:"destination"`
	Method      string `json:"method"`
	Path        string `json:"path"`
}

func (s *Server) testPolicy(c *gin.Context) {
	var req testRequest
	if err := c.ShouldBindJSON(&req); err != nil {
		c.JSON(http.StatusBadRequest, gin.H{"error": err.Error()})
		return
	}
	decision := s.engine.Evaluate(policy.Request{
		Source:      req.Source,
		Destination: req.Destination,
		Method:      req.Method,
		Path:        req.Path,
	})
	c.JSON(http.StatusOK, decision)
}

func (s *Server) listIdentities(c *gin.Context) {
	list, err := s.registry.List(c.Request.Context())
	if err != nil {
		c.JSON(http.StatusInternalServerError, gin.H{"error": err.Error()})
		return
	}
	c.JSON(http.StatusOK, list)
}

func (s *Server) registerIdentity(c *gin.Context) {
	var si registry.ServiceIdentity
	if err := c.ShouldBindJSON(&si); err != nil {
		c.JSON(http.StatusBadRequest, gin.H{"error": err.Error()})
		return
	}
	if err := s.registry.Register(c.Request.Context(), si); err != nil {
		c.JSON(http.StatusInternalServerError, gin.H{"error": err.Error()})
		return
	}
	s.auditLogger.Log(c.Request.Context(), audit.Entry{
		Action:   "identity.register",
		Operator: c.ClientIP(),
		Message:  si.SPIFFEID,
	})
	c.JSON(http.StatusCreated, si)
}

func (s *Server) deleteIdentity(c *gin.Context) {
	id := c.Param("id")
	if err := s.registry.Deregister(c.Request.Context(), id); err != nil {
		c.JSON(http.StatusInternalServerError, gin.H{"error": err.Error()})
		return
	}
	c.Status(http.StatusNoContent)
}

func (s *Server) listSVIDs(c *gin.Context) {
	c.JSON(http.StatusOK, s.identity.AllSVIDs())
}

func (s *Server) getJWT(c *gin.Context) {
	aud := c.Query("audience")
	if aud == "" {
		aud = "default"
	}
	svid, err := s.identity.GetJWTSVID(aud)
	if err != nil {
		c.JSON(http.StatusInternalServerError, gin.H{"error": err.Error()})
		return
	}
	c.JSON(http.StatusOK, gin.H{
		"spiffe_id":  svid.ID.String(),
		"issued_at":  svid.IssuedAt,
		"expires_at": svid.ExpiresAt,
	})
}

func (s *Server) listAudit(c *gin.Context) {
	limit := int64(100)
	list, err := s.store.ListAudit(c.Request.Context(), limit)
	if err != nil {
		c.JSON(http.StatusInternalServerError, gin.H{"error": err.Error()})
		return
	}
	c.Data(http.StatusOK, "application/json", joinJSONArray(list))
}

func (s *Server) getDegradeStatus(c *gin.Context) {
	info := s.identity.DegradeInfo()
	if info == nil {
		c.JSON(http.StatusOK, gin.H{"degraded": false})
		return
	}
	c.JSON(http.StatusOK, gin.H{
		"degraded":     true,
		"spiffe_id":    info.SPIFFEID,
		"expired_at":   info.ExpiredAt,
		"grace_until":  info.GraceUntil,
		"using_expired": info.UsingExpired,
	})
}

func randomHex(n int) string {
	const letters = "abcdef0123456789"
	b := make([]byte, n)
	for i := range b {
		b[i] = letters[i%len(letters)]
	}
	return string(b)
}

func joinJSONArray(items [][]byte) []byte {
	if len(items) == 0 {
		return []byte("[]")
	}
	out := []byte("[")
	for i, it := range items {
		if i > 0 {
			out = append(out, ',')
		}
		out = append(out, it...)
	}
	out = append(out, ']')
	return out
}
