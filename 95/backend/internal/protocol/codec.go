package protocol

import (
	"encoding/binary"
	"errors"
	"math"
	"time"
)

const (
	MaxGamepads   = 4
	DatagramMTU   = 1200
	MaxButtons    = 17
	MaxAxes       = 6
)

const (
	MsgTypeInput       byte = 0x01
	MsgTypeAck         byte = 0x02
	MsgTypePing        byte = 0x03
	MsgTypePong        byte = 0x04
	MsgTypeMapping     byte = 0x05
	MsgTypeHandshake   byte = 0x06
	MsgTypeStats       byte = 0x07
	MsgTypeState       byte = 0x08
	MsgTypeCorrection  byte = 0x09
	MsgTypeInputAck    byte = 0x0A
	MsgTypePlayerPos   byte = 0x0B
)

type InputPacket struct {
	Seq        uint64
	Timestamp  uint64
	PadIndex   uint8
	Buttons    [MaxButtons]bool
	Axes       [MaxAxes]float32
}

type PingPacket struct {
	Seq       uint64
	Timestamp uint64
	Padding   uint16
}

type StatsPacket struct {
	Seq            uint64
	ReceivedSeq    uint64
	LastRTTms      uint16
	LostPackets    uint32
	TotalPackets   uint32
	ReorderEvents  uint64
}

func EncodeInput(p *InputPacket) ([]byte, error) {
	buf := make([]byte, 0, 64)
	buf = append(buf, MsgTypeInput)
	buf = appendUvarint(buf, p.Seq)
	buf = appendUvarint(buf, p.Timestamp)
	buf = append(buf, p.PadIndex)
	buttonByte := byte(0)
	for i, b := range p.Buttons {
		if b {
			buttonByte |= 1 << (uint(i) % 8)
		}
		if (i+1)%8 == 0 {
			buf = append(buf, buttonByte)
			buttonByte = 0
		}
	}
	buf = append(buf, buttonByte)
	for _, a := range p.Axes {
		buf = appendFloat32(buf, a)
	}
	return buf, nil
}

func DecodeInput(data []byte) (*InputPacket, error) {
	if len(data) < 3 || data[0] != MsgTypeInput {
		return nil, errors.New("invalid input packet")
	}
	p := &InputPacket{}
	off := 1
	seq, n := binary.Uvarint(data[off:])
	if n <= 0 {
		return nil, errors.New("bad seq")
	}
	p.Seq = seq
	off += n
	ts, n := binary.Uvarint(data[off:])
	if n <= 0 {
		return nil, errors.New("bad ts")
	}
	p.Timestamp = ts
	off += n
	if off >= len(data) {
		return nil, errors.New("truncated")
	}
	p.PadIndex = data[off]
	off++
	buttonBytes := (MaxButtons + 7) / 8
	if off+buttonBytes > len(data) {
		return nil, errors.New("truncated buttons")
	}
	for i := 0; i < MaxButtons; i++ {
		p.Buttons[i] = (data[off+i/8]>>(uint(i)%8))&1 == 1
	}
	off += buttonBytes
	axisBytes := MaxAxes * 4
	if off+axisBytes > len(data) {
		return nil, errors.New("truncated axes")
	}
	for i := 0; i < MaxAxes; i++ {
		p.Axes[i] = readFloat32(data[off+i*4:])
	}
	return p, nil
}

func EncodePing(p *PingPacket) []byte {
	buf := make([]byte, 0, 20)
	buf = append(buf, MsgTypePing)
	buf = appendUvarint(buf, p.Seq)
	buf = appendUvarint(buf, p.Timestamp)
	buf = append(buf, byte(p.Padding), byte(p.Padding>>8))
	return buf
}

func DecodePing(data []byte) (*PingPacket, error) {
	if len(data) < 2 || data[0] != MsgTypePing {
		return nil, errors.New("invalid ping")
	}
	p := &PingPacket{}
	off := 1
	seq, n := binary.Uvarint(data[off:])
	if n <= 0 {
		return nil, errors.New("bad seq")
	}
	p.Seq = seq
	off += n
	ts, n := binary.Uvarint(data[off:])
	if n <= 0 {
		return nil, errors.New("bad ts")
	}
	p.Timestamp = ts
	off += n
	if off+2 <= len(data) {
		p.Padding = uint16(data[off]) | uint16(data[off+1])<<8
	}
	return p, nil
}

func EncodePong(p *PingPacket) []byte {
	buf := make([]byte, 0, 20)
	buf = append(buf, MsgTypePong)
	buf = appendUvarint(buf, p.Seq)
	buf = appendUvarint(buf, p.Timestamp)
	buf = appendUvarint(buf, uint64(time.Now().UnixMilli()))
	return buf
}

func DecodePong(data []byte) (seq uint64, origTs uint64, recvTs uint64, err error) {
	if len(data) < 2 || data[0] != MsgTypePong {
		err = errors.New("invalid pong")
		return
	}
	off := 1
	seq, n := binary.Uvarint(data[off:])
	if n <= 0 {
		err = errors.New("bad seq")
		return
	}
	off += n
	origTs, n = binary.Uvarint(data[off:])
	if n <= 0 {
		err = errors.New("bad ts")
		return
	}
	off += n
	recvTs, n = binary.Uvarint(data[off:])
	if n <= 0 {
		err = errors.New("bad recv ts")
		return
	}
	return
}

func EncodeAck(seq uint64) []byte {
	buf := make([]byte, 0, 10)
	buf = append(buf, MsgTypeAck)
	buf = appendUvarint(buf, seq)
	return buf
}

func DecodeAck(data []byte) (uint64, error) {
	if len(data) < 2 || data[0] != MsgTypeAck {
		return 0, errors.New("invalid ack")
	}
	seq, n := binary.Uvarint(data[1:])
	if n <= 0 {
		return 0, errors.New("bad seq")
	}
	return seq, nil
}

func EncodeStats(p *StatsPacket) []byte {
	buf := make([]byte, 0, 40)
	buf = append(buf, MsgTypeStats)
	buf = appendUvarint(buf, p.Seq)
	buf = appendUvarint(buf, p.ReceivedSeq)
	buf = append(buf, byte(p.LastRTTms), byte(p.LastRTTms>>8))
	buf = appendUvarint(buf, uint64(p.LostPackets))
	buf = appendUvarint(buf, uint64(p.TotalPackets))
	buf = appendUvarint(buf, p.ReorderEvents)
	return buf
}

func DecodeStats(data []byte) (*StatsPacket, error) {
	if len(data) < 2 || data[0] != MsgTypeStats {
		return nil, errors.New("invalid stats")
	}
	p := &StatsPacket{}
	off := 1
	seq, n := binary.Uvarint(data[off:])
	if n <= 0 {
		return nil, errors.New("bad seq")
	}
	p.Seq = seq
	off += n
	rseq, n := binary.Uvarint(data[off:])
	if n <= 0 {
		return nil, errors.New("bad rseq")
	}
	p.ReceivedSeq = rseq
	off += n
	if off+2 > len(data) {
		return nil, errors.New("truncated rtt")
	}
	p.LastRTTms = uint16(data[off]) | uint16(data[off+1])<<8
	off += 2
	lost, n := binary.Uvarint(data[off:])
	if n <= 0 {
		return nil, errors.New("bad lost")
	}
	p.LostPackets = uint32(lost)
	off += n
	total, n := binary.Uvarint(data[off:])
	if n <= 0 {
		return nil, errors.New("bad total")
	}
	p.TotalPackets = uint32(total)
	off += n
	re, n := binary.Uvarint(data[off:])
	if n > 0 {
		p.ReorderEvents = re
	}
	return p, nil
}

func appendUvarint(buf []byte, x uint64) []byte {
	tmp := make([]byte, binary.MaxVarintLen64)
	n := binary.PutUvarint(tmp, x)
	return append(buf, tmp[:n]...)
}

func appendFloat32(buf []byte, f float32) []byte {
	b := make([]byte, 4)
	binary.LittleEndian.PutUint32(b, uint32(math.Float32bits(f)))
	return append(buf, b...)
}

func readFloat32(b []byte) float32 {
	return math.Float32frombits(binary.LittleEndian.Uint32(b))
}

func appendFloat64(buf []byte, f float64) []byte {
	b := make([]byte, 8)
	binary.LittleEndian.PutUint64(b, math.Float64bits(f))
	return append(buf, b...)
}

func readFloat64(b []byte) float64 {
	return math.Float64frombits(binary.LittleEndian.Uint64(b))
}

type EntityState struct {
	PlayerID int32
	PosX     float64
	PosY     float64
	VelX     float64
	VelY     float64
}

type StatePacket struct {
	Frame     uint64
	Timestamp uint64
	Entities  []EntityState
}

type CorrectionPacket struct {
	PlayerID    int32
	Frame       uint64
	CorrectPosX float64
	CorrectPosY float64
	CorrectVelX float64
	CorrectVelY float64
	YourPosX    float64
	YourPosY    float64
}

type InputAckPacket struct {
	Seq    uint16
	Frame  uint64
}

func EncodeState(p *StatePacket) []byte {
	buf := make([]byte, 0, 128)
	buf = append(buf, MsgTypeState)
	buf = appendUvarint(buf, p.Frame)
	buf = appendUvarint(buf, p.Timestamp)
	buf = appendUvarint(buf, uint64(len(p.Entities)))
	for _, e := range p.Entities {
		buf = append(buf, byte(e.PlayerID), byte(e.PlayerID>>8), byte(e.PlayerID>>16), byte(e.PlayerID>>24))
		buf = appendFloat64(buf, e.PosX)
		buf = appendFloat64(buf, e.PosY)
		buf = appendFloat64(buf, e.VelX)
		buf = appendFloat64(buf, e.VelY)
	}
	return buf
}

func DecodeState(data []byte) (*StatePacket, error) {
	if len(data) < 2 || data[0] != MsgTypeState {
		return nil, errors.New("invalid state")
	}
	p := &StatePacket{}
	off := 1
	frame, n := binary.Uvarint(data[off:])
	if n <= 0 {
		return nil, errors.New("bad frame")
	}
	p.Frame = frame
	off += n
	ts, n := binary.Uvarint(data[off:])
	if n <= 0 {
		return nil, errors.New("bad ts")
	}
	p.Timestamp = ts
	off += n
	cnt, n := binary.Uvarint(data[off:])
	if n <= 0 {
		return nil, errors.New("bad cnt")
	}
	off += n
	for i := uint64(0); i < cnt; i++ {
		if off+4+32 > len(data) {
			break
		}
		pid := int32(binary.LittleEndian.Uint32(data[off:]))
		off += 4
		px := readFloat64(data[off:])
		off += 8
		py := readFloat64(data[off:])
		off += 8
		vx := readFloat64(data[off:])
		off += 8
		vy := readFloat64(data[off:])
		off += 8
		p.Entities = append(p.Entities, EntityState{
			PlayerID: pid, PosX: px, PosY: py, VelX: vx, VelY: vy,
		})
	}
	return p, nil
}

func EncodeCorrection(p *CorrectionPacket) []byte {
	buf := make([]byte, 0, 72)
	buf = append(buf, MsgTypeCorrection)
	buf = append(buf, byte(p.PlayerID), byte(p.PlayerID>>8), byte(p.PlayerID>>16), byte(p.PlayerID>>24))
	buf = appendUvarint(buf, p.Frame)
	buf = appendFloat64(buf, p.CorrectPosX)
	buf = appendFloat64(buf, p.CorrectPosY)
	buf = appendFloat64(buf, p.CorrectVelX)
	buf = appendFloat64(buf, p.CorrectVelY)
	buf = appendFloat64(buf, p.YourPosX)
	buf = appendFloat64(buf, p.YourPosY)
	return buf
}

func DecodeCorrection(data []byte) (*CorrectionPacket, error) {
	if len(data) < 6 || data[0] != MsgTypeCorrection {
		return nil, errors.New("invalid correction")
	}
	p := &CorrectionPacket{}
	off := 1
	p.PlayerID = int32(binary.LittleEndian.Uint32(data[off:]))
	off += 4
	frame, n := binary.Uvarint(data[off:])
	if n <= 0 {
		return nil, errors.New("bad frame")
	}
	p.Frame = frame
	off += n
	if off+48 > len(data) {
		return nil, errors.New("truncated")
	}
	p.CorrectPosX = readFloat64(data[off:])
	off += 8
	p.CorrectPosY = readFloat64(data[off:])
	off += 8
	p.CorrectVelX = readFloat64(data[off:])
	off += 8
	p.CorrectVelY = readFloat64(data[off:])
	off += 8
	p.YourPosX = readFloat64(data[off:])
	off += 8
	p.YourPosY = readFloat64(data[off:])
	return p, nil
}

func EncodeInputAck(p *InputAckPacket) []byte {
	buf := make([]byte, 0, 12)
	buf = append(buf, MsgTypeInputAck)
	buf = append(buf, byte(p.Seq), byte(p.Seq>>8))
	buf = appendUvarint(buf, p.Frame)
	return buf
}

func DecodeInputAck(data []byte) (*InputAckPacket, error) {
	if len(data) < 3 || data[0] != MsgTypeInputAck {
		return nil, errors.New("invalid input ack")
	}
	p := &InputAckPacket{}
	p.Seq = uint16(data[1]) | uint16(data[2])<<8
	off := 3
	frame, n := binary.Uvarint(data[off:])
	if n <= 0 {
		return nil, errors.New("bad frame")
	}
	p.Frame = frame
	return p, nil
}

type PlayerPosPacket struct {
	Seq        uint16
	PlayerID   int32
	PredictedX float64
	PredictedY float64
	Timestamp  uint64
}

func EncodePlayerPos(p *PlayerPosPacket) []byte {
	buf := make([]byte, 0, 36)
	buf = append(buf, MsgTypePlayerPos)
	buf = append(buf, byte(p.Seq), byte(p.Seq>>8))
	buf = append(buf, byte(p.PlayerID), byte(p.PlayerID>>8), byte(p.PlayerID>>16), byte(p.PlayerID>>24))
	buf = appendFloat64(buf, p.PredictedX)
	buf = appendFloat64(buf, p.PredictedY)
	buf = appendUvarint(buf, p.Timestamp)
	return buf
}

func DecodePlayerPos(data []byte) (*PlayerPosPacket, error) {
	if len(data) < 8 || data[0] != MsgTypePlayerPos {
		return nil, errors.New("invalid player pos")
	}
	p := &PlayerPosPacket{}
	off := 1
	p.Seq = uint16(data[off]) | uint16(data[off+1])<<8
	off += 2
	p.PlayerID = int32(binary.LittleEndian.Uint32(data[off:]))
	off += 4
	if off+16 > len(data) {
		return nil, errors.New("truncated pos")
	}
	p.PredictedX = readFloat64(data[off:])
	off += 8
	p.PredictedY = readFloat64(data[off:])
	off += 8
	if off < len(data) {
		ts, n := binary.Uvarint(data[off:])
		if n > 0 {
			p.Timestamp = ts
		}
	}
	return p, nil
}
