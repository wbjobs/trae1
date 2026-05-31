package transport

import (
	"encoding/binary"
	"encoding/json"
	"fmt"
	"io"
	"net"
)

func writeMessage(conn net.Conn, msgType MessageType, payload interface{}) error {
	var payloadData []byte
	if payload != nil {
		var err error
		payloadData, err = json.Marshal(payload)
		if err != nil {
			return fmt.Errorf("marshal payload: %w", err)
		}
	}

	totalLen := uint32(1 + len(payloadData))
	header := make([]byte, 5)
	binary.BigEndian.PutUint32(header[:4], totalLen)
	header[4] = byte(msgType)

	if _, err := conn.Write(header); err != nil {
		return fmt.Errorf("write message header: %w", err)
	}

	if len(payloadData) > 0 {
		if _, err := conn.Write(payloadData); err != nil {
			return fmt.Errorf("write message payload: %w", err)
		}
	}

	return nil
}

func readMessage(conn net.Conn) (*Message, error) {
	header := make([]byte, 5)
	if _, err := io.ReadFull(conn, header); err != nil {
		return nil, fmt.Errorf("read message header: %w", err)
	}

	totalLen := binary.BigEndian.Uint32(header[:4])
	msgType := MessageType(header[4])

	payloadLen := totalLen - 1
	var payloadData []byte
	if payloadLen > 0 {
		payloadData = make([]byte, payloadLen)
		if _, err := io.ReadFull(conn, payloadData); err != nil {
			return nil, fmt.Errorf("read message payload: %w", err)
		}
	}

	return &Message{
		Type:    msgType,
		Payload: json.RawMessage(payloadData),
	}, nil
}

func writeHandshake(conn net.Conn) error {
	_, err := conn.Write([]byte(HandshakeSig))
	return err
}

func readHandshake(conn net.Conn) error {
	buf := make([]byte, len(HandshakeSig))
	if _, err := io.ReadFull(conn, buf); err != nil {
		return fmt.Errorf("read handshake: %w", err)
	}
	if string(buf) != HandshakeSig {
		return fmt.Errorf("invalid handshake: expected %s, got %s", HandshakeSig, string(buf))
	}
	return nil
}
