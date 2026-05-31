package physics

import (
	"math"
	"sync"
)

const (
	FixedTimestep = 1.0 / 60.0
	MaxVelocity   = 500.0
	Friction      = 0.92
	Acceleration  = 1200.0
)

type Vec2 struct {
	X, Y float64
}

func (v Vec2) Add(o Vec2) Vec2  { return Vec2{v.X + o.X, v.Y + o.Y} }
func (v Vec2) Sub(o Vec2) Vec2  { return Vec2{v.X - o.X, v.Y - o.Y} }
func (v Vec2) Mul(s float64) Vec2 { return Vec2{v.X * s, v.Y * s} }
func (v Vec2) Len() float64      { return math.Sqrt(v.X*v.X + v.Y*v.Y) }
func (v Vec2) Normalize() Vec2 {
	l := v.Len()
	if l < 1e-6 {
		return Vec2{}
	}
	return v.Mul(1 / l)
}

type Input struct {
	MoveX    float64
	MoveY    float64
	Buttons  [17]bool
	Sequence uint16
}

type Entity struct {
	Pos      Vec2
	Vel      Vec2
	Radius   float64
	PlayerID int
}

type Obstacle struct {
	Min, Max Vec2
}

type World struct {
	mu        sync.Mutex
	Entities  []Entity
	Obstacles []Obstacle
	Time      float64
	Frame     uint64
	rng       *DeterministicRNG
}

type DeterministicRNG struct {
	state uint64
}

func NewDeterministicRNG(seed uint64) *DeterministicRNG {
	return &DeterministicRNG{state: seed}
}

func (r *DeterministicRNG) Next() uint32 {
	r.state = r.state*6364136223846793005 + 1442695040888963407
	return uint32(r.state >> 32)
}

func (r *DeterministicRNG) Float() float64 {
	return float64(r.Next()) / float64(^uint32(0))
}

func (r *DeterministicRNG) Range(min, max float64) float64 {
	return min + r.Float()*(max-min)
}

func NewWorld() *World {
	return &World{
		rng: NewDeterministicRNG(12345),
		Obstacles: []Obstacle{
			{Min: Vec2{-400, -300}, Max: Vec2{-350, 300}},
			{Min: Vec2{350, -300}, Max: Vec2{400, 300}},
			{Min: Vec2{-400, -300}, Max: Vec2{400, -250}},
			{Min: Vec2{-400, 250}, Max: Vec2{400, 300}},
			{Min: Vec2{-100, -100}, Max: Vec2{100, 100}},
		},
	}
}

func (w *World) AddPlayer(id int) {
	w.mu.Lock()
	defer w.mu.Unlock()
	for _, e := range w.Entities {
		if e.PlayerID == id {
			return
		}
	}
	w.Entities = append(w.Entities, Entity{
		Pos:      Vec2{float64(id*50 - 75), 0},
		Vel:      Vec2{},
		Radius:   20,
		PlayerID: id,
	})
}

func (w *World) RemovePlayer(id int) {
	w.mu.Lock()
	defer w.mu.Unlock()
	for i, e := range w.Entities {
		if e.PlayerID == id {
			w.Entities = append(w.Entities[:i], w.Entities[i+1:]...)
			return
		}
	}
}

func (w *World) Step(inputs map[int]Input) {
	w.mu.Lock()
	defer w.mu.Unlock()
	w.Frame++
	w.Time += FixedTimestep

	for playerIdx := range w.Entities {
		e := &w.Entities[playerIdx]
		input, ok := inputs[e.PlayerID]
		if !ok {
			e.Vel = e.Vel.Mul(Friction)
		} else {
			move := Vec2{input.MoveX, input.MoveY}
			if move.Len() > 1 {
				move = move.Normalize()
			}
			accel := move.Mul(Acceleration * FixedTimestep)
			e.Vel = e.Vel.Add(accel)
			e.Vel = e.Vel.Mul(Friction)
			if e.Vel.Len() > MaxVelocity {
				e.Vel = e.Vel.Normalize().Mul(MaxVelocity)
			}
		}

		delta := e.Vel.Mul(FixedTimestep)
		newPos := e.Pos.Add(delta)

		for _, obs := range w.Obstacles {
			if collides(newPos, e.Radius, obs) {
				normal := resolveCollision(e.Pos, newPos, e.Radius, obs)
				newPos = e.Pos.Add(delta.Sub(normal.Mul(delta.Dot(normal) * 1.01)))
				e.Vel = e.Vel.Sub(normal.Mul(e.Vel.Dot(normal) * 1.01))
				break
			}
		}

		e.Pos = newPos
	}
}

func collides(pos Vec2, r float64, obs Obstacle) bool {
	closest := Vec2{
		clamp(pos.X, obs.Min.X, obs.Max.X),
		clamp(pos.Y, obs.Min.Y, obs.Max.Y),
	}
	dist := pos.Sub(closest).Len()
	return dist < r
}

func resolveCollision(old, new Vec2, r float64, obs Obstacle) Vec2 {
	cx := clamp(new.X, obs.Min.X, obs.Max.X)
	cy := clamp(new.Y, obs.Min.Y, obs.Max.Y)
	dx := new.X - cx
	dy := new.Y - cy
	if dx == 0 && dy == 0 {
		if old.X < obs.Min.X {
			return Vec2{-1, 0}
		} else if old.X > obs.Max.X {
			return Vec2{1, 0}
		} else if old.Y < obs.Min.Y {
			return Vec2{0, -1}
		}
		return Vec2{0, 1}
	}
	dist := math.Sqrt(dx*dx + dy*dy)
	return Vec2{dx / dist, dy / dist}.Normalize()
}

func (w *World) GetPlayer(id int) (Entity, bool) {
	w.mu.Lock()
	defer w.mu.Unlock()
	for _, e := range w.Entities {
		if e.PlayerID == id {
			return e, true
		}
	}
	return Entity{}, false
}

func (w *World) Snapshot() []Entity {
	w.mu.Lock()
	defer w.mu.Unlock()
	out := make([]Entity, len(w.Entities))
	copy(out, w.Entities)
	return out
}

func (w *World) FrameNumber() uint64 {
	w.mu.Lock()
	defer w.mu.Unlock()
	return w.Frame
}

func clamp(v, lo, hi float64) float64 {
	if v < lo {
		return lo
	}
	if v > hi {
		return hi
	}
	return v
}
