package proxy

import (
	"context"
	"encoding/json"
	"fmt"
	"net"
	"time"

	"github.com/grpctest/grpctest/internal/schema"
	"github.com/grpctest/grpctest/internal/session"
	"github.com/jhump/protoreflect/desc"
	"github.com/jhump/protoreflect/dynamic"
	"google.golang.org/grpc"
	"google.golang.org/grpc/credentials/insecure"
	"google.golang.org/grpc/metadata"
)

type Options struct {
	ListenAddr     string
	TargetAddr     string
	TargetInsecure bool
	Schema         *schema.Provider
	Timeout        time.Duration
}

type Proxy struct {
	opts     Options
	server   *grpc.Server
	upstream *grpc.ClientConn
	session  *session.Session
}

func New(ctx context.Context, opts Options) (*Proxy, error) {
	if opts.Schema == nil {
		return nil, fmt.Errorf("schema provider is required")
	}
	dialCtx, cancel := context.WithTimeout(ctx, opts.Timeout)
	defer cancel()
	dialOpts := []grpc.DialOption{grpc.WithBlock()}
	if opts.TargetInsecure {
		dialOpts = append(dialOpts, grpc.WithTransportCredentials(insecure.NewCredentials()))
	}
	conn, err := grpc.DialContext(dialCtx, opts.TargetAddr, dialOpts...)
	if err != nil {
		return nil, fmt.Errorf("dial upstream: %w", err)
	}
	p := &Proxy{
		opts:     opts,
		upstream: conn,
		session: &session.Session{
			Version:  1,
			Target:   opts.TargetAddr,
			Insecure: opts.TargetInsecure,
			Timeout:  opts.Timeout.String(),
			Created:  time.Now().UTC(),
		},
	}
	return p, nil
}

func (p *Proxy) Session() *session.Session { return p.session }

func (p *Proxy) Close() error {
	if p.server != nil {
		p.server.GracefulStop()
	}
	if p.upstream != nil {
		return p.upstream.Close()
	}
	return nil
}

func (p *Proxy) Run(ctx context.Context) error {
	lis, err := net.Listen("tcp", p.opts.ListenAddr)
	if err != nil {
		return fmt.Errorf("listen %s: %w", p.opts.ListenAddr, err)
	}
	p.server = grpc.NewServer(
		grpc.UnknownServiceHandler(p.handle),
	)
	go func() {
		<-ctx.Done()
		p.server.GracefulStop()
	}()
	return p.server.Serve(lis)
}

func (p *Proxy) handle(_ interface{}, serverStream grpc.ServerStream) error {
	ctx := serverStream.Context()
	fullMethod, ok := grpc.Method(ctx)
	if !ok {
		return fmt.Errorf("cannot determine gRPC method from context")
	}
	svc, method := splitMethod(fullMethod)

	md, err := p.opts.Schema.FindMethod(svc, method)
	if err != nil {
		return fmt.Errorf("unknown method %s/%s: %w (enable use_reflection or provide proto files)", svc, method, err)
	}

	if md.IsClientStreaming() || md.IsServerStreaming() {
		return fmt.Errorf("streaming methods are not supported for recording: %s", fullMethod)
	}

	inMsg := dynamic.NewMessage(md.GetInputType())
	if err := serverStream.RecvMsg(inMsg); err != nil {
		return fmt.Errorf("receive request: %w", err)
	}

	entry := session.Entry{
		ID:       fmt.Sprintf("%s_%s_%d", shortName(svc), method, len(p.session.Entries)+1),
		Service:  svc,
		Method:   method,
		Recorded: time.Now().UTC(),
	}

	if inMD, ok := metadata.FromIncomingContext(ctx); ok {
		m := map[string]string{}
		for k, vs := range inMD {
			if len(vs) > 0 {
				m[k] = vs[0]
			}
		}
		entry.Metadata = m
	}

	if reqBytes, err := inMsg.MarshalJSON(); err == nil {
		_ = json.Unmarshal(reqBytes, &entry.Request)
	}

	start := time.Now()
	respMsg, callErr := p.forward(ctx, md, inMsg)
	entry.Duration = time.Since(start)

	if callErr == nil {
		if respBytes, err := respMsg.MarshalJSON(); err == nil {
			_ = json.Unmarshal(respBytes, &entry.Response)
		}
		if err := serverStream.SendMsg(respMsg); err != nil {
			return err
		}
	} else {
		entry.Error = callErr.Error()
		return callErr
	}

	p.session.Entries = append(p.session.Entries, entry)
	return nil
}

func (p *Proxy) forward(ctx context.Context, md *desc.MethodDescriptor, req *dynamic.Message) (*dynamic.Message, error) {
	callCtx, cancel := context.WithTimeout(ctx, p.opts.Timeout)
	defer cancel()

	if inMD, ok := metadata.FromIncomingContext(ctx); ok {
		callCtx = metadata.NewOutgoingContext(callCtx, inMD)
	}

	stub := schema.NewStub(p.upstream)
	resp, err := stub.InvokeRpc(callCtx, md, req)
	if err != nil {
		return nil, err
	}
	dm, ok := resp.(*dynamic.Message)
	if !ok {
		return nil, fmt.Errorf("unexpected response type %T", resp)
	}
	return dm, nil
}

func splitMethod(full string) (string, string) {
	if len(full) > 0 && full[0] == '/' {
		full = full[1:]
	}
	for i := len(full) - 1; i >= 0; i-- {
		if full[i] == '/' {
			return full[:i], full[i+1:]
		}
	}
	return "", full
}

func shortName(full string) string {
	for i := len(full) - 1; i >= 0; i-- {
		if full[i] == '.' {
			return full[i+1:]
		}
	}
	return full
}
