package main

import (
	"flag"
	"fmt"
	"os"
	"time"

	tea "github.com/charmbracelet/bubbletea"

	"v2v/monitor/protocol"
	"v2v/monitor/radar"
	"v2v/monitor/source"
)

type tickMsg time.Time

func tickCmd() tea.Cmd {
	return tea.Tick(100*time.Millisecond, func(t time.Time) tea.Msg {
		return tickMsg(t)
	})
}

type model struct {
	source protocol.DataSource
	peers  []protocol.PeerData
	width  int
	height int
}

func (m model) Init() tea.Cmd {
	return tickCmd()
}

func (m model) Update(msg tea.Msg) (tea.Model, tea.Cmd) {
	switch msg := msg.(type) {
	case tea.KeyMsg:
		switch msg.String() {
		case "q", "ctrl+c":
			return m, tea.Quit
		}

	case tea.WindowSizeMsg:
		m.width = msg.Width
		m.height = msg.Height

	case tickMsg:
		m.peers = m.source.Peers()
		return m, tickCmd()
	}

	return m, nil
}

func (m model) View() string {
	if m.width == 0 || m.height == 0 {
		return "Initializing..."
	}
	return radar.Render(m.width, m.height, m.peers)
}

func main() {
	mock := flag.Bool("mock", false, "Use mock data source instead of serial")
	port := flag.String("port", "/dev/ttyACM0", "Serial port for ESP32")
	baud := flag.Int("baud", 115200, "Serial baud rate")
	flag.Parse()

	var src protocol.DataSource
	if *mock {
		src = source.NewMockSource()
	} else {
		src = source.NewSerialSource(*port, *baud)
	}

	if err := src.Start(); err != nil {
		fmt.Fprintf(os.Stderr, "Failed to start data source: %v\n", err)
		os.Exit(1)
	}
	defer src.Close()

	p := tea.NewProgram(
		model{source: src},
		tea.WithAltScreen(),
	)
	if _, err := p.Run(); err != nil {
		fmt.Fprintf(os.Stderr, "Error: %v\n", err)
		os.Exit(1)
	}
}
