package replay

import (
	"context"
	"encoding/json"
	"fmt"
	"time"

	"github.com/golang/protobuf/proto"
	"github.com/grpctest/grpctest/internal/diff"
	"github.com/grpctest/grpctest/internal/schema"
	"github.com/grpctest/grpctest/internal/session"
	"github.com/grpctest/grpctest/internal/vars"
	"github.com/grpctest/grpctest/internal/wkt"
	"github.com/jhump/protoreflect/dynamic"
	"github.com/jhump/protoreflect/dynamic/grpcdynamic"
	"google.golang.org/grpc"
	"google.golang.org/grpc/credentials/insecure"
	"google.golang.org/grpc/metadata"
)

type Options struct {
	Session      *session.Session
	ExtraIgnores []string
}

type EntryResult struct {
	ID       string
	Service  string
	Method   string
	Passed   bool
	Error    string
	Duration time.Duration
	Request  map[string]interface{}
	Expected map[string]interface{}
	Actual   map[string]interface{}
	Diff     diff.Report
}

type ReplayStats struct {
	Total       int
	Passed      int
	Failed      int
	Error       int
	SuccessRate float64
}

type ReplayResult struct {
	Entries []EntryResult
	Stats   ReplayStats
}

func Run(ctx context.Context, opts Options) (*ReplayResult, error) {
	if opts.Session == nil {
		return nil, fmt.Errorf("session is required")
	}
	timeout := parseTimeout(opts.Session.Timeout, 10*time.Second)

	prov, err := schema.NewProvider(ctx, schema.Options{
		ProtoFiles:    opts.Session.ProtoFiles,
		ImportPaths:   opts.Session.ImportPaths,
		UseReflection: opts.Session.UseReflect,
		Address:       opts.Session.Target,
		Insecure:      opts.Session.Insecure,
		Timeout:       timeout,
	})
	if err != nil {
		return nil, fmt.Errorf("init schema: %w", err)
	}
	defer prov.Close()

	dialCtx, cancel := context.WithTimeout(ctx, timeout)
	defer cancel()
	dialOpts := []grpc.DialOption{grpc.WithBlock()}
	if opts.Session.Insecure {
		dialOpts = append(dialOpts, grpc.WithTransportCredentials(insecure.NewCredentials()))
	}
	conn, err := grpc.DialContext(dialCtx, opts.Session.Target, dialOpts...)
	if err != nil {
		return nil, fmt.Errorf("dial target: %w", err)
	}
	defer conn.Close()
	stub := grpcdynamic.NewStub(conn)
	_ = schema.NewStub

	store := vars.NewStore()
	result := &ReplayResult{}

	for _, e := range opts.Session.Entries {
		er := replayEntry(ctx, prov, stub, store, e, timeout, opts.ExtraIgnores)
		result.Entries = append(result.Entries, er)
	}
	result.Stats = buildStats(result.Entries)
	return result, nil
}

func replayEntry(ctx context.Context, prov *schema.Provider, stub grpcdynamic.Stub, store *vars.Store, e session.Entry, timeout time.Duration, extraIgnores []string) EntryResult {
	er := EntryResult{
		ID:       e.ID,
		Service:  e.Service,
		Method:   e.Method,
		Request:  e.Request,
		Expected: e.Response,
	}
	md, err := prov.FindMethod(e.Service, e.Method)
	if err != nil {
		er.Error = fmt.Sprintf("find method: %v", err)
		return er
	}

	reqMsg := dynamic.NewMessage(md.GetInputType())
	reqData := store.SubstituteAny(e.Request)
	if reqData != nil {
		transformed, err := wkt.TransformRequest(md.GetInputType(), reqData, "")
		if err != nil {
			er.Error = fmt.Sprintf("transform request: %v", err)
			return er
		}
		b, _ := json.Marshal(transformed)
		if err := reqMsg.UnmarshalJSON(b); err != nil {
			er.Error = fmt.Sprintf("build request: %v", err)
			return er
		}
	}

	callCtx, cancel := context.WithTimeout(ctx, timeout)
	defer cancel()
	if len(e.Metadata) > 0 {
		pairs := make([]string, 0, len(e.Metadata)*2)
		for k, v := range e.Metadata {
			pairs = append(pairs, k, store.Substitute(v))
		}
		callCtx = metadata.NewOutgoingContext(callCtx, metadata.Pairs(pairs...))
	}

	start := time.Now()
	var protoReq proto.Message = reqMsg
	resp, err := stub.InvokeRpc(callCtx, md, protoReq)
	er.Duration = time.Since(start)
	if err != nil {
		er.Error = fmt.Sprintf("call: %v", err)
		return er
	}
	dm, ok := resp.(*dynamic.Message)
	if !ok {
		er.Error = fmt.Sprintf("unexpected response type %T", resp)
		return er
	}
	b, err := dm.MarshalJSON()
	if err != nil {
		er.Error = fmt.Sprintf("marshal response: %v", err)
		return er
	}
	_ = json.Unmarshal(b, &er.Actual)

	ignores := mergeIgnores(e.Ignore, extraIgnores)
	er.Diff = diff.Diff(er.Expected, er.Actual, ignores)
	er.Passed = er.Diff.Identical
	return er
}

func buildStats(entries []EntryResult) ReplayStats {
	s := ReplayStats{Total: len(entries)}
	for _, e := range entries {
		switch {
		case e.Error != "":
			s.Error++
		case !e.Passed:
			s.Failed++
		default:
			s.Passed++
		}
	}
	if s.Total > 0 {
		s.SuccessRate = float64(s.Passed) / float64(s.Total) * 100
	}
	return s
}

func parseTimeout(s string, def time.Duration) time.Duration {
	if s == "" {
		return def
	}
	d, err := time.ParseDuration(s)
	if err != nil {
		return def
	}
	return d
}

func mergeIgnores(a, b []string) []string {
	out := make([]string, 0, len(a)+len(b))
	seen := map[string]bool{}
	for _, s := range a {
		if !seen[s] {
			seen[s] = true
			out = append(out, s)
		}
	}
	for _, s := range b {
		if !seen[s] {
			seen[s] = true
			out = append(out, s)
		}
	}
	return out
}
