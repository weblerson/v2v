package source

import (
	"v2v/monitor/protocol"
)

// SerialSource reads NDJSON peer data from the ESP32 serial port.
//
// TODO: implement once the firmware emits structured JSON over Serial.
// Plan:
//   - Open the serial port (e.g. /dev/ttyACM0) at 115200 baud using
//     go.bug.st/serial.
//   - Spawn a goroutine that reads lines with bufio.Scanner.
//   - Parse each line as a protocol.PeerData JSON object.
//   - Store the latest data per MAC (keyed map), protected by sync.RWMutex.
//   - Peers() returns a snapshot of the map.
//   - Stale entries (no update for >1.5s) are pruned on each read.
type SerialSource struct {
	port string
	baud int
}

func NewSerialSource(port string, baud int) *SerialSource {
	return &SerialSource{port: port, baud: baud}
}

func (s *SerialSource) Start() error {
	// TODO: open port, start reader goroutine.
	return nil
}

func (s *SerialSource) Peers() []protocol.PeerData {
	// TODO: return current peer snapshot.
	return nil
}

func (s *SerialSource) Close() error {
	// TODO: close port, stop goroutine.
	return nil
}
