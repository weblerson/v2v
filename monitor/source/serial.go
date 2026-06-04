package source

import (
	"bufio"
	"encoding/json"
	"fmt"
	"sync"
	"time"

	"go.bug.st/serial"

	"v2v/monitor/protocol"
)

const staleAfter = 1500 * time.Millisecond

// SerialSource reads NDJSON peer data from the ESP32 serial port.
//
// Firmware emits one JSON object per line:
//   {"mac":"44:17:93:4C:7F:90","distance":12.3,"bearing":45.0,"state":"BRAKING"}
type SerialSource struct {
	port string
	baud int

	mu     sync.RWMutex
	peers  map[string]peerEntry
	port_  serial.Port
	stopCh chan struct{}
}

type peerEntry struct {
	data    protocol.PeerData
	lastRx  time.Time
}

func NewSerialSource(port string, baud int) *SerialSource {
	return &SerialSource{
		port:  port,
		baud:  baud,
		peers: make(map[string]peerEntry),
	}
}

func (s *SerialSource) Start() error {
	mode := &serial.Mode{BaudRate: s.baud}
	p, err := serial.Open(s.port, mode)
	if err != nil {
		return fmt.Errorf("open %s: %w", s.port, err)
	}
	s.port_ = p
	s.stopCh = make(chan struct{})
	go s.read()
	return nil
}

func (s *SerialSource) read() {
	scanner := bufio.NewScanner(s.port_)
	scanner.Buffer(make([]byte, 0, 4096), 64*1024)
	for scanner.Scan() {
		select {
		case <-s.stopCh:
			return
		default:
		}
		line := scanner.Bytes()
		if len(line) == 0 || line[0] != '{' {
			continue
		}
		var pd protocol.PeerData
		if err := json.Unmarshal(line, &pd); err != nil {
			continue
		}
		if pd.MAC == "" {
			continue
		}
		s.mu.Lock()
		s.peers[pd.MAC] = peerEntry{data: pd, lastRx: time.Now()}
		s.mu.Unlock()
	}
}

func (s *SerialSource) Peers() []protocol.PeerData {
	now := time.Now()
	s.mu.Lock()
	defer s.mu.Unlock()

	out := make([]protocol.PeerData, 0, len(s.peers))
	for mac, entry := range s.peers {
		if now.Sub(entry.lastRx) > staleAfter {
			delete(s.peers, mac)
			continue
		}
		out = append(out, entry.data)
	}
	return out
}

func (s *SerialSource) Close() error {
	if s.stopCh != nil {
		close(s.stopCh)
	}
	if s.port_ != nil {
		return s.port_.Close()
	}
	return nil
}
