package main

import (
	"encoding/binary"
	"fmt"
	"log"
	"net"
	"sync"
	"syscall"
)

type RSTSender struct {
	fd  int
	mu  sync.Mutex
	off bool
}

func NewRSTSender() (*RSTSender, error) {
	fd, err := syscall.Socket(syscall.AF_INET, syscall.SOCK_RAW, syscall.IPPROTO_RAW)
	if err != nil {
		return nil, fmt.Errorf("raw socket: %w", err)
	}
	if err := syscall.SetsockoptInt(fd, syscall.IPPROTO_IP, syscall.IP_HDRINCL, 1); err != nil {
		syscall.Close(fd)
		return nil, fmt.Errorf("IP_HDRINCL: %w", err)
	}
	return &RSTSender{fd: fd}, nil
}

func (s *RSTSender) Close() {
	s.mu.Lock()
	defer s.mu.Unlock()
	if !s.off {
		syscall.Close(s.fd)
		s.off = true
	}
}

func (s *RSTSender) SendRST(srcIP, dstIP net.IP, srcPort, dstPort uint16, seq, ack uint32) error {
	s.mu.Lock()
	defer s.mu.Unlock()
	if s.off {
		return fmt.Errorf("sender closed")
	}

	src4 := srcIP.To4()
	dst4 := dstIP.To4()
	if src4 == nil || dst4 == nil {
		return fmt.Errorf("non-IPv4 address")
	}

	pkt := buildTCPRST(src4, dst4, srcPort, dstPort, seq, ack)
	addr := syscall.SockaddrInet4{
		Port: int(dstPort),
	}
	copy(addr.Addr[:], dst4)

	return syscall.Sendto(s.fd, pkt, 0, &addr)
}

func (s *RSTSender) SendRSTBoth(srcIP, dstIP net.IP, srcPort, dstPort uint16, seq, ack uint32) {
	if err := s.SendRST(srcIP, dstIP, srcPort, dstPort, seq, ack); err != nil {
		log.Printf("RST -> dst: %v", err)
	}
	if err := s.SendRST(dstIP, srcIP, dstPort, srcPort, ack, seq); err != nil {
		log.Printf("RST -> src: %v", err)
	}
}

func buildTCPRST(srcIP, dstIP net.IP, srcPort, dstPort uint16, seq, ack uint32) []byte {
	ipHdr := make([]byte, 20)
	ipHdr[0] = 0x45
	ipHdr[1] = 0
	totalLen := uint16(20 + 20)
	binary.BigEndian.PutUint16(ipHdr[2:4], totalLen)
	binary.BigEndian.PutUint16(ipHdr[4:6], 0)
	ipHdr[6] = 0
	ipHdr[7] = 0
	ipHdr[8] = 64
	ipHdr[9] = syscall.IPPROTO_TCP
	copy(ipHdr[12:16], srcIP.To4())
	copy(ipHdr[16:20], dstIP.To4())
	putChecksum(ipHdr[10:12], ipChecksum(ipHdr))

	tcpHdr := make([]byte, 20)
	binary.BigEndian.PutUint16(tcpHdr[0:2], srcPort)
	binary.BigEndian.PutUint16(tcpHdr[2:4], dstPort)
	binary.BigEndian.PutUint32(tcpHdr[4:8], seq)
	binary.BigEndian.PutUint32(tcpHdr[8:12], ack)
	tcpHdr[12] = 5 << 4
	tcpHdr[13] = 0x14
	binary.BigEndian.PutUint16(tcpHdr[14:16], 0)
	binary.BigEndian.PutUint16(tcpHdr[16:18], tcpChecksum(srcIP.To4(), dstIP.To4(), tcpHdr))
	binary.BigEndian.PutUint16(tcpHdr[18:20], 0)

	return append(ipHdr, tcpHdr...)
}

func ipChecksum(data []byte) uint16 {
	var sum uint32
	n := len(data)
	for i := 0; i+1 < n; i += 2 {
		sum += uint32(binary.BigEndian.Uint16(data[i : i+2]))
	}
	if n%2 == 1 {
		sum += uint32(data[n-1]) << 8
	}
	for (sum >> 16) != 0 {
		sum = (sum & 0xffff) + (sum >> 16)
	}
	return ^uint16(sum)
}

func tcpChecksum(srcIP, dstIP net.IP, tcp []byte) uint16 {
	pseudo := make([]byte, 12)
	copy(pseudo[0:4], srcIP)
	copy(pseudo[4:8], dstIP)
	pseudo[8] = 0
	pseudo[9] = syscall.IPPROTO_TCP
	binary.BigEndian.PutUint16(pseudo[10:12], uint16(len(tcp)))

	buf := append(pseudo, tcp...)
	if len(buf)%2 == 1 {
		buf = append(buf, 0)
	}
	var sum uint32
	for i := 0; i < len(buf); i += 2 {
		sum += uint32(binary.BigEndian.Uint16(buf[i : i+2]))
	}
	for (sum >> 16) != 0 {
		sum = (sum & 0xffff) + (sum >> 16)
	}
	return ^uint16(sum)
}

func putChecksum(target []byte, csum uint16) {
	binary.BigEndian.PutUint16(target, csum)
}
