package main

import (
	"encoding/json"
	"fmt"
	"net"
	"os"
	"strings"
)

type WhitelistRule struct {
	SrcCIDR string `json:"src_cidr"`
	DstCIDR string `json:"dst_cidr"`
	Port    uint16 `json:"port"`
	Proto   string `json:"proto"`
	Comment string `json:"comment,omitempty"`
}

type WhitelistConfig struct {
	Rules []WhitelistRule `json:"rules"`
}

type CompiledRule struct {
	srcNet  *net.IPNet
	dstNet  *net.IPNet
	port    uint16
	proto   uint8
	comment string
}

type Whitelist struct {
	rules []CompiledRule
}

func LoadWhitelist(path string) (*Whitelist, error) {
	if path == "" {
		return &Whitelist{}, nil
	}
	f, err := os.Open(path)
	if err != nil {
		return nil, fmt.Errorf("open whitelist: %w", err)
	}
	defer f.Close()

	var cfg WhitelistConfig
	if err := json.NewDecoder(f).Decode(&cfg); err != nil {
		return nil, fmt.Errorf("parse whitelist: %w", err)
	}
	return compileWhitelist(&cfg)
}

func compileWhitelist(cfg *WhitelistConfig) (*Whitelist, error) {
	w := &Whitelist{rules: make([]CompiledRule, 0, len(cfg.Rules))}
	for i, r := range cfg.Rules {
		cr, err := compileRule(r)
		if err != nil {
			return nil, fmt.Errorf("rule %d: %w", i, err)
		}
		w.rules = append(w.rules, cr)
	}
	return w, nil
}

func compileRule(r WhitelistRule) (CompiledRule, error) {
	cr := CompiledRule{
		port:    r.Port,
		comment: r.Comment,
	}
	switch strings.ToLower(r.Proto) {
	case "", "tcp":
		cr.proto = uint8(ProtoTCP)
	case "udp":
		cr.proto = uint8(ProtoUDP)
	default:
		return cr, fmt.Errorf("unknown proto: %s", r.Proto)
	}
	if r.SrcCIDR != "" {
		_, n, err := net.ParseCIDR(r.SrcCIDR)
		if err != nil {
			return cr, fmt.Errorf("src_cidr: %w", err)
		}
		cr.srcNet = n
	}
	if r.DstCIDR != "" {
		_, n, err := net.ParseCIDR(r.DstCIDR)
		if err != nil {
			return cr, fmt.Errorf("dst_cidr: %w", err)
		}
		cr.dstNet = n
	}
	return cr, nil
}

func (w *Whitelist) Contains(srcIP, dstIP net.IP, dstPort uint16, proto Proto) bool {
	if w == nil || len(w.rules) == 0 {
		return false
	}
	for _, r := range w.rules {
		if r.proto != uint8(proto) {
			continue
		}
		if r.port != 0 && r.port != dstPort {
			continue
		}
		if r.srcNet != nil && !r.srcNet.Contains(srcIP) {
			continue
		}
		if r.dstNet != nil && !r.dstNet.Contains(dstIP) {
			continue
		}
		return true
	}
	return false
}

func (w *Whitelist) Len() int {
	if w == nil {
		return 0
	}
	return len(w.rules)
}
