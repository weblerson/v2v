package protocol

// PeerData represents one peer's current state as emitted by the firmware
// over serial (one JSON object per line).
//
// Firmware serial format (NDJSON):
//
//	{"mac":"44:17:93:4C:7F:90","distance":3.24,"bearing":0.0,
//	 "bearing_valid":false,"closing":1.85,"ttc":1.8,"state":"BRAKING"}
type PeerData struct {
	MAC      string  `json:"mac"`
	Distance float64 `json:"distance"` // meters
	Bearing  float64 `json:"bearing"`  // degrees, 0 = ahead, clockwise

	// BearingValid reports whether Bearing carries real information.
	//
	// It is false on the UWB backend, which is the normal case: a single
	// DWM1000 antenna measures time of flight, not direction. Rather than
	// drawing every vehicle straight ahead, the radar renders these peers as a
	// ring at the measured radius — "somewhere at this distance" is exactly
	// what the hardware knows.
	BearingValid bool `json:"bearing_valid"`

	// Closing is the rate at which the gap is shrinking, in m/s. Negative
	// means the peer is pulling away.
	Closing float64 `json:"closing"`

	// TTC is the time to collision in seconds, or -1 when the pair is not
	// converging fast enough for the figure to mean anything.
	TTC float64 `json:"ttc"`

	State string `json:"state"` // IDLE, BRAKING, ACCELERATING
}

// DataSource abstracts where peer data comes from. Two implementations:
//   - SerialSource: reads NDJSON from the ESP32 serial port (production)
//   - MockSource:   generates fake peers for development/testing
type DataSource interface {
	// Start initializes the source (open port, spawn goroutines, etc.).
	Start() error
	// Peers returns a snapshot of all currently known peers.
	Peers() []PeerData
	// Close tears down the source and releases resources.
	Close() error
}
