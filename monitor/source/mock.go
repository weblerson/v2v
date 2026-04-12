package source

import (
	"math"
	"sync"
	"time"

	"v2v/monitor/protocol"
)

// mockPeer holds animation state for one simulated car.
type mockPeer struct {
	mac          string
	bearing      float64 // current degrees (0 = north, clockwise)
	distance     float64 // current meters
	bearingSpeed float64 // degrees per tick
	distMin      float64
	distMax      float64
	distPeriod   float64 // seconds for one full oscillation
	distPhase    float64 // current phase in radians
	state        string
}

// MockSource generates fake peer data for UI development.
type MockSource struct {
	mu       sync.RWMutex
	peers    []mockPeer
	stopCh   chan struct{}
	interval time.Duration
}

func NewMockSource() *MockSource {
	return &MockSource{
		interval: 100 * time.Millisecond,
		peers: []mockPeer{
			{
				// Car circling around us (overtaking simulation).
				mac: "AA:BB:CC:DD:01:01", bearing: 0, distance: 15,
				bearingSpeed: 2.0, distMin: 10, distMax: 25, distPeriod: 8,
				state: "IDLE",
			},
			{
				// Car ahead, approaching and receding (braking/accelerating).
				mac: "AA:BB:CC:DD:02:02", bearing: 0, distance: 30,
				bearingSpeed: 0, distMin: 3, distMax: 40, distPeriod: 12,
				state: "BRAKING",
			},
			{
				// Car behind, slowly drifting left.
				mac: "AA:BB:CC:DD:03:03", bearing: 180, distance: 20,
				bearingSpeed: -0.5, distMin: 15, distMax: 35, distPeriod: 15,
				state: "ACCELERATING",
			},
		},
	}
}

func (m *MockSource) Start() error {
	m.stopCh = make(chan struct{})
	go m.run()
	return nil
}

func (m *MockSource) run() {
	ticker := time.NewTicker(m.interval)
	defer ticker.Stop()
	for {
		select {
		case <-m.stopCh:
			return
		case <-ticker.C:
			m.tick()
		}
	}
}

func (m *MockSource) tick() {
	dt := m.interval.Seconds()
	m.mu.Lock()
	defer m.mu.Unlock()

	for i := range m.peers {
		p := &m.peers[i]

		// Update bearing.
		p.bearing += p.bearingSpeed * dt * 10
		if p.bearing >= 360 {
			p.bearing -= 360
		} else if p.bearing < 0 {
			p.bearing += 360
		}

		// Oscillate distance.
		p.distPhase += (2 * math.Pi / p.distPeriod) * dt
		mid := (p.distMax + p.distMin) / 2
		amp := (p.distMax - p.distMin) / 2
		p.distance = mid + amp*math.Sin(p.distPhase)

		// Update motion state based on distance trend.
		deriv := amp * math.Cos(p.distPhase) * (2 * math.Pi / p.distPeriod)
		switch {
		case deriv < -1:
			p.state = "BRAKING"
		case deriv > 1:
			p.state = "ACCELERATING"
		default:
			p.state = "IDLE"
		}
	}
}

func (m *MockSource) Peers() []protocol.PeerData {
	m.mu.RLock()
	defer m.mu.RUnlock()

	out := make([]protocol.PeerData, len(m.peers))
	for i, p := range m.peers {
		out[i] = protocol.PeerData{
			MAC:      p.mac,
			Distance: math.Round(p.distance*10) / 10,
			Bearing:  math.Round(p.bearing*10) / 10,
			State:    p.state,
		}
	}
	return out
}

func (m *MockSource) Close() error {
	close(m.stopCh)
	return nil
}
