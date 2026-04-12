package protocol

// PeerData represents one peer's current state as emitted by the firmware
// over serial (one JSON object per line).
//
// Firmware serial format (NDJSON):
//   {"mac":"44:17:93:4C:7F:90","distance":12.3,"bearing":45.0,"state":"BRAKING"}
type PeerData struct {
	MAC      string  `json:"mac"`
	Distance float64 `json:"distance"` // meters
	Bearing  float64 `json:"bearing"`  // degrees, 0 = north (ahead), clockwise
	State    string  `json:"state"`    // IDLE, BRAKING, ACCELERATING
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
