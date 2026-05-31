package schema

import (
	"context"
	"fmt"
	"time"

	"github.com/jhump/protoreflect/desc"
	"github.com/jhump/protoreflect/desc/protoparse"
	"github.com/jhump/protoreflect/dynamic/grpcdynamic"
	"github.com/jhump/protoreflect/grpcreflect"
	"google.golang.org/grpc"
	"google.golang.org/grpc/credentials/insecure"
)

type Provider struct {
	protoFiles    []*desc.FileDescriptor
	reflectClient *grpcreflect.Client
	conn          *grpc.ClientConn
	useReflect    bool
}

type Options struct {
	ProtoFiles    []string
	ImportPaths   []string
	UseReflection bool
	Address       string
	Insecure      bool
	Timeout       time.Duration
}

func NewProvider(ctx context.Context, opts Options) (*Provider, error) {
	p := &Provider{useReflect: opts.UseReflection}

	if len(opts.ProtoFiles) > 0 {
		parser := protoparse.Parser{
			ImportPaths: append([]string{"."}, opts.ImportPaths...),
		}
		fds, err := parser.ParseFiles(opts.ProtoFiles...)
		if err != nil {
			return nil, fmt.Errorf("parse proto files: %w", err)
		}
		p.protoFiles = append(p.protoFiles, fds...)
	}

	if opts.UseReflection {
		connCtx, cancel := context.WithTimeout(ctx, opts.Timeout)
		defer cancel()
		dialOpts := []grpc.DialOption{
			grpc.WithBlock(),
		}
		if opts.Insecure {
			dialOpts = append(dialOpts, grpc.WithTransportCredentials(insecure.NewCredentials()))
		}
		conn, err := grpc.DialContext(connCtx, opts.Address, dialOpts...)
		if err != nil {
			return nil, fmt.Errorf("dial for reflection: %w", err)
		}
		p.conn = conn
		p.reflectClient = grpcreflect.NewClientAuto(context.Background(), conn)
	}
	return p, nil
}

func (p *Provider) Close() error {
	if p.reflectClient != nil {
		p.reflectClient.Reset()
	}
	if p.conn != nil {
		return p.conn.Close()
	}
	return nil
}

func (p *Provider) FindMethod(service, method string) (*desc.MethodDescriptor, error) {
	for _, fd := range p.protoFiles {
		for _, svc := range fd.GetServices() {
			if svc.GetFullyQualifiedName() == service || svc.GetName() == service {
				for _, m := range svc.GetMethods() {
					if m.GetName() == method {
						return m, nil
					}
				}
			}
		}
	}
	if p.reflectClient != nil {
		svcNames, err := p.reflectClient.ListServices()
		if err != nil {
			return nil, fmt.Errorf("list services via reflection: %w", err)
		}
		var foundSvc string
		for _, s := range svcNames {
			if s == service || shortName(s) == service {
				foundSvc = s
				break
			}
		}
		if foundSvc == "" {
			return nil, fmt.Errorf("service %q not found via reflection (have: %v)", service, svcNames)
		}
		sd, err := p.reflectClient.ResolveService(foundSvc)
		if err != nil {
			return nil, fmt.Errorf("resolve service %q: %w", foundSvc, err)
		}
		for _, m := range sd.GetMethods() {
			if m.GetName() == method {
				return m, nil
			}
		}
		return nil, fmt.Errorf("method %q not found in service %q", method, foundSvc)
	}
	return nil, fmt.Errorf("method %s/%s not found", service, method)
}

func shortName(full string) string {
	for i := len(full) - 1; i >= 0; i-- {
		if full[i] == '.' {
			return full[i+1:]
		}
	}
	return full
}

func NewStub(conn *grpc.ClientConn) grpcdynamic.Stub {
	return grpcdynamic.NewStub(conn)
}
