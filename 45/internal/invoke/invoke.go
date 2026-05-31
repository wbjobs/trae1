package invoke

import (
	"context"
	"encoding/json"
	"fmt"
	"time"

	"github.com/grpctest/grpctest/internal/config"
	"github.com/grpctest/grpctest/internal/schema"
	"github.com/grpctest/grpctest/internal/vars"
	"github.com/grpctest/grpctest/internal/wkt"
	"github.com/jhump/protoreflect/desc"
	"github.com/jhump/protoreflect/dynamic"
	"github.com/jhump/protoreflect/dynamic/grpcdynamic"
	"google.golang.org/grpc"
	"google.golang.org/grpc/credentials/insecure"
	"google.golang.org/grpc/metadata"
)

type Caller struct {
	conn    *grpc.ClientConn
	stub    grpcdynamic.Stub
	timeout time.Duration
}

func NewCaller(ctx context.Context, t config.Target) (*Caller, error) {
	opts := []grpc.DialOption{
		grpc.WithBlock(),
	}
	if t.Insecure {
		opts = append(opts, grpc.WithTransportCredentials(insecure.NewCredentials()))
	}
	dialCtx, cancel := context.WithTimeout(ctx, t.Timeout)
	defer cancel()
	conn, err := grpc.DialContext(dialCtx, t.Address, opts...)
	if err != nil {
		return nil, fmt.Errorf("dial %s: %w", t.Address, err)
	}
	return &Caller{
		conn:    conn,
		stub:    schema.NewStub(conn),
		timeout: t.Timeout,
	}, nil
}

func (c *Caller) Close() error {
	if c.conn != nil {
		return c.conn.Close()
	}
	return nil
}

type CallResult struct {
	Response map[string]interface{}
	Err      error
	Duration time.Duration
}

func (c *Caller) Invoke(ctx context.Context, md *desc.MethodDescriptor, step config.Step, store *vars.Store) CallResult {
	start := time.Now()
	reqMsg := dynamic.NewMessage(md.GetInputType())

	reqData := store.SubstituteAny(step.Request)
	if step.RequestRaw != "" {
		substituted := store.Substitute(step.RequestRaw)
		var raw interface{}
		if err := json.Unmarshal([]byte(substituted), &raw); err != nil {
			return CallResult{Err: fmt.Errorf("parse request_raw: %w", err), Duration: time.Since(start)}
		}
		reqData = raw
	}

	if reqData != nil {
		transformed, err := wkt.TransformRequest(md.GetInputType(), reqData, "")
		if err != nil {
			return CallResult{Err: fmt.Errorf("transform request: %w", err), Duration: time.Since(start)}
		}
		reqBytes, err := json.Marshal(transformed)
		if err != nil {
			return CallResult{Err: fmt.Errorf("marshal request: %w", err), Duration: time.Since(start)}
		}
		if err := reqMsg.UnmarshalJSON(reqBytes); err != nil {
			return CallResult{Err: fmt.Errorf("build request: %w", err), Duration: time.Since(start)}
		}
	}

	callCtx, cancel := context.WithTimeout(ctx, c.timeout)
	defer cancel()

	if len(step.Metadata) > 0 {
		pairs := make([]string, 0, len(step.Metadata)*2)
		for k, v := range step.Metadata {
			pairs = append(pairs, k, store.Substitute(v))
		}
		callCtx = metadata.NewOutgoingContext(callCtx, metadata.Pairs(pairs...))
	}

	var respHeader, respTrailer metadata.MD
	respMsg, err := c.stub.InvokeRpc(callCtx, md, reqMsg,
		grpc.Header(&respHeader),
		grpc.Trailer(&respTrailer),
	)
	result := CallResult{Duration: time.Since(start)}
	if err != nil {
		result.Err = err
		return result
	}
	if respMsg == nil {
		result.Response = map[string]interface{}{}
		return result
	}
	dynMsg, ok := respMsg.(*dynamic.Message)
	if !ok {
		result.Err = fmt.Errorf("unexpected response type %T", respMsg)
		return result
	}
	raw, err := dynMsg.MarshalJSON()
	if err != nil {
		result.Err = fmt.Errorf("marshal response: %w", err)
		return result
	}
	var m map[string]interface{}
	if err := json.Unmarshal(raw, &m); err != nil {
		result.Err = fmt.Errorf("decode response: %w", err)
		return result
	}
	result.Response = m
	return result
}
