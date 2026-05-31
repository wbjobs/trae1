package ssh

import (
	"context"
	"fmt"
	"io"
	"net"
	"os"
	"path/filepath"
	"sync"
	"time"

	"bastion/internal/config"
	"bastion/internal/models"

	gliderssh "github.com/gliderlabs/ssh"
	gossh "golang.org/x/crypto/ssh"
)

type SessionHook func(ctx context.Context, session *models.Session, recordFile string) error

type CommandInterceptor interface {
	Intercept(command string, session *models.Session) (allow bool, message string)
	OnSessionComplete(session *models.Session)
}

type Proxy struct {
	cfg              *config.Config
	store            *models.SessionStore
	recordDir        string
	onSessionComplete []SessionHook
	interceptor      CommandInterceptor
}

func NewProxy(cfg *config.Config, store *models.SessionStore) *Proxy {
	return &Proxy{
		cfg:       cfg,
		store:     store,
		recordDir: cfg.RecordDir,
		onSessionComplete: make([]SessionHook, 0),
	}
}

func (p *Proxy) SetInterceptor(interceptor CommandInterceptor) {
	p.interceptor = interceptor
}

func (p *Proxy) OnSessionComplete(hook SessionHook) {
	p.onSessionComplete = append(p.onSessionComplete, hook)
}

func (p *Proxy) Handler() gliderssh.Handler {
	return func(sess gliderssh.Session) {
		ctx := sess.Context()
		user := sess.User()
		clientIP := ctx.RemoteAddr().String()

		session := models.NewSession(
			user,
			p.cfg.SSH.TargetHost,
			p.cfg.SSH.TargetPort,
			p.cfg.SSH.TargetUser,
			clientIP,
		)

		if pty, winCh, isPty := sess.Pty(); isPty {
			session.Width = pty.Window.Width
			session.Height = pty.Window.Height
			go func() {
				for win := range winCh {
					session.Width = win.Width
					session.Height = win.Height
				}
			}()
		}

		p.store.Add(session)

		recordFile := filepath.Join(p.recordDir, fmt.Sprintf("%s.cast", session.ID))
		if err := os.MkdirAll(p.recordDir, 0755); err != nil {
			fmt.Fprintf(sess, "Error creating record directory: %v\r\n", err)
			session.Fail()
			return
		}

		recorder, err := NewRecorder(recordFile, session, session.Width, session.Height)
		if err != nil {
			fmt.Fprintf(sess, "Error creating recorder: %v\r\n", err)
			session.Fail()
			return
		}
		session.FrameStore = recorder.FrameStore()
		recorder.StartFrameCapture()

		targetClient, err := p.dialTarget()
		if err != nil {
			fmt.Fprintf(sess, "Error connecting to target: %v\r\n", err)
			recorder.Close()
			session.Fail()
			return
		}
		defer targetClient.Close()

		targetSession, err := targetClient.NewSession()
		if err != nil {
			fmt.Fprintf(sess, "Error creating target session: %v\r\n", err)
			recorder.Close()
			session.Fail()
			return
		}
		defer targetSession.Close()

		if pty, _, isPty := sess.Pty(); isPty {
			modes := gossh.TerminalModes{
				gossh.ECHO:          1,
				gossh.TTY_OP_ISPEED: 14400,
				gossh.TTY_OP_OSPEED: 14400,
			}
			if err := targetSession.RequestPty(pty.Term, pty.Window.Width, pty.Window.Height, modes); err != nil {
				fmt.Fprintf(sess, "Error requesting PTY: %v\r\n", err)
				recorder.Close()
				session.Fail()
				return
			}
		}

		if err := targetSession.Shell(); err != nil {
			fmt.Fprintf(sess, "Error starting shell: %v\r\n", err)
			recorder.Close()
			session.Fail()
			return
		}

		targetStdin, err := targetSession.StdinPipe()
		if err != nil {
			fmt.Fprintf(sess, "Error creating stdin pipe: %v\r\n", err)
			recorder.Close()
			session.Fail()
			return
		}

		targetStdout, err := targetSession.StdoutPipe()
		if err != nil {
			fmt.Fprintf(sess, "Error creating stdout pipe: %v\r\n", err)
			recorder.Close()
			session.Fail()
			return
		}

		targetStderr, err := targetSession.StderrPipe()
		if err != nil {
			fmt.Fprintf(sess, "Error creating stderr pipe: %v\r\n", err)
			recorder.Close()
			session.Fail()
			return
		}

		go func() {
			ir := &inputRecorder{
				recorder:    recorder,
				session:     session,
				targetStdin: targetStdin,
				clientOut:   sess,
				interceptor: p.interceptor,
			}
			io.Copy(ir, sess)
		}()

		var wg sync.WaitGroup
		wg.Add(2)

		go func() {
			defer wg.Done()
			writer := NewTeeWriter(sess, &outputRecorder{recorder: recorder})
			io.Copy(writer, targetStdout)
		}()

		go func() {
			defer wg.Done()
			writer := NewTeeWriter(sess.Stderr(), &outputRecorder{recorder: recorder})
			io.Copy(writer, targetStderr)
		}()

		wg.Wait()
		recorder.Close()
		targetSession.Wait()

		if p.interceptor != nil {
			p.interceptor.OnSessionComplete(session)
		}

		objectKey := fmt.Sprintf("%s%s.cast", p.cfg.MinIO.SessionPrefix, session.ID)
		session.Complete(recordFile, objectKey)

		p.afterSession(session)
	}
}

func (p *Proxy) dialTarget() (*gossh.Client, error) {
	var authMethods []gossh.AuthMethod

	if p.cfg.SSH.TargetPassword != "" {
		authMethods = append(authMethods, gossh.Password(p.cfg.SSH.TargetPassword))
	}

	if p.cfg.SSH.TargetKeyFile != "" {
		key, err := os.ReadFile(p.cfg.SSH.TargetKeyFile)
		if err != nil {
			return nil, fmt.Errorf("read target key: %w", err)
		}
		signer, err := gossh.ParsePrivateKey(key)
		if err != nil {
			return nil, fmt.Errorf("parse target key: %w", err)
		}
		authMethods = append(authMethods, gossh.PublicKeys(signer))
	}

	if len(authMethods) == 0 {
		return nil, fmt.Errorf("no authentication method configured for target")
	}

	config := &gossh.ClientConfig{
		User:            p.cfg.SSH.TargetUser,
		Auth:            authMethods,
		HostKeyCallback: gossh.InsecureIgnoreHostKey(),
		Timeout:         10 * time.Second,
	}

	addr := fmt.Sprintf("%s:%d", p.cfg.SSH.TargetHost, p.cfg.SSH.TargetPort)
	conn, err := net.DialTimeout("tcp", addr, 10*time.Second)
	if err != nil {
		return nil, fmt.Errorf("dial target: %w", err)
	}

	clientConn, chans, reqs, err := gossh.NewClientConn(conn, addr, config)
	if err != nil {
		return nil, fmt.Errorf("new client conn: %w", err)
	}

	return gossh.NewClient(clientConn, chans, reqs), nil
}

func (p *Proxy) afterSession(session *models.Session) {
	ctx := context.Background()
	recordFile := session.RecordFile
	for _, hook := range p.onSessionComplete {
		if err := hook(ctx, session, recordFile); err != nil {
			fmt.Printf("[Proxy] Session hook error: %v\n", err)
		}
	}
}

type inputRecorder struct {
	recorder    *Recorder
	session     *models.Session
	targetStdin io.Writer
	clientOut   io.Writer
	interceptor CommandInterceptor
	buf         []byte
}

func (ir *inputRecorder) Write(p []byte) (int, error) {
	ir.recorder.WriteInput(p)
	ir.buf = append(ir.buf, p...)

	for len(ir.buf) > 0 {
		if idx := indexOfNewline(ir.buf); idx >= 0 {
			line := string(ir.buf[:idx])
			newline := ir.buf[idx : idx+1]
			ir.buf = ir.buf[idx+1:]

			if len(line) > 0 {
				elapsed := time.Since(ir.recorder.startTime).Seconds()
				ir.session.AddCommand(elapsed, line)

				if ir.interceptor != nil {
					allow, msg := ir.interceptor.Intercept(line, ir.session)
					if !allow {
						if msg != "" && ir.clientOut != nil {
							fmt.Fprintf(ir.clientOut, "\r\n\x1b[31m%s\x1b[0m\r\n", msg)
						}
						continue
					}
				}
			}

			fullCmd := append([]byte(line), newline...)
			if ir.targetStdin != nil {
				if _, err := ir.targetStdin.Write(fullCmd); err != nil {
					return 0, err
				}
			}
		} else {
			break
		}
	}
	return len(p), nil
}

func indexOfNewline(b []byte) int {
	for i, v := range b {
		if v == '\n' || v == '\r' {
			return i
		}
	}
	return -1
}

type outputRecorder struct {
	recorder *Recorder
}

func (or *outputRecorder) Write(p []byte) (int, error) {
	or.recorder.WriteOutput(p)
	return len(p), nil
}
