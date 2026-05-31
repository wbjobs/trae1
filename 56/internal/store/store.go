package store

import (
	"context"
	"encoding/json"
	"fmt"
	"net/netip"
	"sort"
	"strconv"
	"strings"
	"time"

	"github.com/redis/go-redis/v9"

	"iprep-sync/internal/config"
	"iprep-sync/internal/model"
)

type Store struct {
	rdb    *redis.Client
	prefix string
}

type peerRecord struct {
	Peer      string           `json:"peer"`
	PeerASN   uint32           `json:"peer_asn"`
	Prefix    string           `json:"prefix"`
	Level     model.ThreatLevel `json:"level"`
	UpdatedAt time.Time        `json:"updated_at"`
	Raw       string           `json:"raw,omitempty"`
}

type mergedRecord struct {
	Prefix    string           `json:"prefix"`
	Level     model.ThreatLevel `json:"level"`
	Sources   []peerRecord     `json:"sources"`
	UpdatedAt time.Time        `json:"updated_at"`
}

func New(cfg config.Redis) *Store {
	rdb := redis.NewClient(&redis.Options{
		Addr:     cfg.Addr,
		Password: cfg.Password,
		DB:       cfg.DB,
	})
	return &Store{rdb: rdb, prefix: cfg.KeyPrefix}
}

func (s *Store) Client() *redis.Client { return s.rdb }

func (s *Store) key(parts ...string) string {
	return s.prefix + strings.Join(parts, ":")
}

func afKey(addr netip.Addr) string {
	if addr.Is4() || addr.Is4In6() {
		return "4"
	}
	return "6"
}

func maskToBin(pfx netip.Prefix) string {
	addr := pfx.Addr().Unmap()
	bits := pfx.Bits()
	if addr.Is4() || addr.Is4In6() {
		v := addr.As4()
		var sb strings.Builder
		for i := 0; i < bits; i++ {
			b := v[i/8] & (1 << (7 - i%8))
			if b != 0 {
				sb.WriteByte('1')
			} else {
				sb.WriteByte('0')
			}
		}
		return sb.String()
	}
	v := addr.As16()
	var sb strings.Builder
	for i := 0; i < bits; i++ {
		b := v[i/8] & (1 << (7 - i%8))
		if b != 0 {
			sb.WriteByte('1')
		} else {
			sb.WriteByte('0')
		}
	}
	return sb.String()
}

func ipToBin32(ip netip.Addr) string {
	ip = ip.Unmap()
	if ip.Is4() || ip.Is4In6() {
		v := ip.As4()
		var sb strings.Builder
		for i := 0; i < 32; i++ {
			b := v[i/8] & (1 << (7 - i%8))
			if b != 0 {
				sb.WriteByte('1')
			} else {
				sb.WriteByte('0')
			}
		}
		return sb.String()
	}
	v := ip.As16()
	var sb strings.Builder
	for i := 0; i < 128; i++ {
		b := v[i/8] & (1 << (7 - i%8))
		if b != 0 {
			sb.WriteByte('1')
		} else {
			sb.WriteByte('0')
		}
	}
	return sb.String()
}

func (s *Store) indexKey(addr netip.Addr) string {
	return s.key("lpm", afKey(addr))
}

func (s *Store) peerKey(prefix, peer string) string {
	return s.key("prefix", prefix, "peer", peer)
}

func (s *Store) mergedKey(prefix string) string {
	return s.key("prefix", prefix, "merged")
}

func (s *Store) peersSetKey(prefix string) string {
	return s.key("prefix", prefix, "peers")
}

// UpsertPeerPrefix 写入或更新某 peer 对某前缀的威胁等级，并重算合并结果
func (s *Store) UpsertPeerPrefix(ctx context.Context, peer string, peerASN uint32, prefix netip.Prefix, level model.ThreatLevel, raw string) error {
	prefix = prefix.Masked()
	pfxStr := prefix.String()
	rec := peerRecord{
		Peer:      peer,
		PeerASN:   peerASN,
		Prefix:    pfxStr,
		Level:     level,
		UpdatedAt: time.Now().UTC(),
		Raw:       raw,
	}
	data, err := json.Marshal(rec)
	if err != nil {
		return err
	}
	pipe := s.rdb.TxPipeline()
	pipe.Set(ctx, s.peerKey(pfxStr, peer), data, 0)
	pipe.SAdd(ctx, s.peersSetKey(pfxStr), peer)
	pipe.ZAdd(ctx, s.indexKey(prefix.Addr()), redis.Z{
		Score:  float64(prefix.Bits()),
		Member: maskToBin(prefix),
	})
	if _, err := pipe.Exec(ctx); err != nil {
		return err
	}
	return s.recomputeMerged(ctx, prefix)
}

// RemovePeerPrefix 某 peer 撤销该前缀；如无其它 peer 提供则整条删除
func (s *Store) RemovePeerPrefix(ctx context.Context, peer string, prefix netip.Prefix) error {
	prefix = prefix.Masked()
	pfxStr := prefix.String()
	pipe := s.rdb.TxPipeline()
	pipe.Del(ctx, s.peerKey(pfxStr, peer))
	pipe.SRem(ctx, s.peersSetKey(pfxStr), peer)
	if _, err := pipe.Exec(ctx); err != nil {
		return err
	}
	return s.recomputeMerged(ctx, prefix)
}

func (s *Store) recomputeMerged(ctx context.Context, prefix netip.Prefix) error {
	pfxStr := prefix.String()
	peers, err := s.rdb.SMembers(ctx, s.peersSetKey(pfxStr)).Result()
	if err != nil && err != redis.Nil {
		return err
	}
	if len(peers) == 0 {
		pipe := s.rdb.TxPipeline()
		pipe.Del(ctx, s.mergedKey(pfxStr))
		pipe.Del(ctx, s.peersSetKey(pfxStr))
		pipe.ZRem(ctx, s.indexKey(prefix.Addr()), maskToBin(prefix))
		_, err := pipe.Exec(ctx)
		return err
	}
	sources := make([]peerRecord, 0, len(peers))
	for _, p := range peers {
		b, err := s.rdb.Get(ctx, s.peerKey(pfxStr, p)).Bytes()
		if err != nil {
			if err == redis.Nil {
				continue
			}
			return err
		}
		var r peerRecord
		if err := json.Unmarshal(b, &r); err != nil {
			return err
		}
		sources = append(sources, r)
	}
	if len(sources) == 0 {
		pipe := s.rdb.TxPipeline()
		pipe.Del(ctx, s.mergedKey(pfxStr))
		pipe.Del(ctx, s.peersSetKey(pfxStr))
		pipe.ZRem(ctx, s.indexKey(prefix.Addr()), maskToBin(prefix))
		_, err := pipe.Exec(ctx)
		return err
	}
	sort.SliceStable(sources, func(i, j int) bool {
		if sources[i].Level != sources[j].Level {
			return sources[i].Level > sources[j].Level
		}
		return sources[i].Peer < sources[j].Peer
	})
	top := sources[0].Level
	merged := mergedRecord{
		Prefix:    pfxStr,
		Level:     top,
		Sources:   sources,
		UpdatedAt: time.Now().UTC(),
	}
	data, err := json.Marshal(merged)
	if err != nil {
		return err
	}
	return s.rdb.Set(ctx, s.mergedKey(pfxStr), data, 0).Err()
}

// LookupIP 对单个 IP 做最长前缀匹配，返回合并后的结果
func (s *Store) LookupIP(ctx context.Context, ipStr string) (*model.QueryResult, error) {
	addr, err := netip.ParseAddr(ipStr)
	if err != nil {
		return nil, fmt.Errorf("invalid ip %q: %w", ipStr, err)
	}
	bin := ipToBin32(addr)
	idxKey := s.indexKey(addr)

	maxBits := float64(32)
	if addr.Is6() && !addr.Is4In6() {
		maxBits = 128
	}
	vals, err := s.rdb.ZRangeByScore(ctx, idxKey, &redis.ZRangeBy{
		Min:    "0",
		Max:    strconv.FormatFloat(maxBits, 'f', -1, 64),
		Offset: 0,
		Count:  int64(int(maxBits) + 1),
	}).Result()
	if err != nil {
		if err == redis.Nil {
			return &model.QueryResult{QueryIP: ipStr, Level: model.LevelUnknown}, nil
		}
		return nil, err
	}
	// 从最长到最短匹配
	for i := len(vals) - 1; i >= 0; i-- {
		pfxBin := vals[i]
		if strings.HasPrefix(bin, pfxBin) {
			pfxStr, err := prefixFromBin(pfxBin, addr)
			if err != nil {
				continue
			}
			b, err := s.rdb.Get(ctx, s.mergedKey(pfxStr)).Bytes()
			if err != nil {
				if err == redis.Nil {
					continue
				}
				return nil, err
			}
			var m mergedRecord
			if err := json.Unmarshal(b, &m); err != nil {
				return nil, err
			}
			sources := make([]model.SourceRecord, 0, len(m.Sources))
			for _, s := range m.Sources {
				sources = append(sources, model.SourceRecord{
					Peer:      s.Peer,
					PeerASN:   s.PeerASN,
					Prefix:    s.Prefix,
					Level:     s.Level,
					UpdatedAt: s.UpdatedAt,
					Raw:       s.Raw,
				})
			}
			return &model.QueryResult{
				QueryIP: ipStr,
				Matched: m.Prefix,
				Level:   m.Level,
				Sources: sources,
			}, nil
		}
	}
	return &model.QueryResult{QueryIP: ipStr, Level: model.LevelUnknown}, nil
}

func prefixFromBin(bin string, sample netip.Addr) (string, error) {
	if sample.Is4() || sample.Is4In6() {
		if len(bin) > 32 {
			return "", fmt.Errorf("bad bin")
		}
		var v4 [4]byte
		for i, c := range bin {
			if c == '1' {
				v4[i/8] |= 1 << (7 - i%8)
			}
		}
		addr := netip.AddrFrom4(v4)
		return netip.PrefixFrom(addr, len(bin)).Masked().String(), nil
	}
	if len(bin) > 128 {
		return "", fmt.Errorf("bad bin")
	}
	var v6 [16]byte
	for i, c := range bin {
		if c == '1' {
			v6[i/8] |= 1 << (7 - i%8)
		}
	}
	addr := netip.AddrFrom16(v6)
	return netip.PrefixFrom(addr, len(bin)).Masked().String(), nil
}

// Stats 返回索引中前缀数量等基本指标
func (s *Store) Stats(ctx context.Context) (map[string]int64, error) {
	res := map[string]int64{}
	for _, af := range []string{"4", "6"} {
		n, err := s.rdb.ZCard(ctx, s.key("lpm", af)).Result()
		if err != nil && err != redis.Nil {
			return nil, err
		}
		res["prefixes_af"+af] = n
	}
	return res, nil
}
