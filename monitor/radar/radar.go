package radar

import (
	"fmt"
	"math"
	"strings"

	"v2v/monitor/protocol"
)

const (
	RedThreshold    = 5.0  // meters — closer than this is danger
	YellowThreshold = 20.0 // meters — closer than this is caution
	MaxRange        = 50.0 // meters — outer edge of the radar
)

// ANSI color codes.
const (
	reset   = "\033[0m"
	red     = "\033[91m"
	yellow  = "\033[93m"
	green   = "\033[92m"
	dim     = "\033[2m"
	bold    = "\033[1m"
	bgRed   = "\033[41m"
	bgYellow = "\033[43m"
	white   = "\033[97m"
	cyan    = "\033[96m"
)

// cell represents one character on the canvas.
type cell struct {
	ch    rune
	color string
}

// canvas is a 2D character buffer.
type canvas struct {
	cells         [][]cell
	width, height int
}

func newCanvas(w, h int) *canvas {
	c := &canvas{width: w, height: h, cells: make([][]cell, h)}
	for y := 0; y < h; y++ {
		c.cells[y] = make([]cell, w)
		for x := 0; x < w; x++ {
			c.cells[y][x] = cell{ch: ' '}
		}
	}
	return c
}

func (c *canvas) set(x, y int, ch rune, color string) {
	if x >= 0 && x < c.width && y >= 0 && y < c.height {
		c.cells[y][x] = cell{ch: ch, color: color}
	}
}

func (c *canvas) setStr(x, y int, s string, color string) {
	for i, ch := range s {
		c.set(x+i, y, ch, color)
	}
}

func (c *canvas) render() string {
	var sb strings.Builder
	for y := 0; y < c.height; y++ {
		for x := 0; x < c.width; x++ {
			cell := c.cells[y][x]
			if cell.color != "" {
				sb.WriteString(cell.color)
				sb.WriteRune(cell.ch)
				sb.WriteString(reset)
			} else {
				sb.WriteRune(cell.ch)
			}
		}
		if y < c.height-1 {
			sb.WriteRune('\n')
		}
	}
	return sb.String()
}

// drawCircle plots a dotted circle on the canvas, accounting for the ~2:1
// terminal character aspect ratio.
func (c *canvas) drawCircle(cx, cy int, r float64, ch rune, color string) {
	steps := int(2 * math.Pi * r * 3)
	if steps < 90 {
		steps = 90
	}
	for i := 0; i < steps; i++ {
		angle := 2 * math.Pi * float64(i) / float64(steps)
		x := cx + int(math.Round(r*math.Sin(angle)*2)) // *2 for aspect ratio
		y := cy - int(math.Round(r*math.Cos(angle)))
		c.set(x, y, ch, color)
	}
}

// drawCrosshair draws faint + lines through the center.
func (c *canvas) drawCrosshair(cx, cy int, r float64, color string) {
	// Vertical line.
	for dy := -int(r); dy <= int(r); dy++ {
		c.set(cx, cy+dy, '·', color)
	}
	// Horizontal line (stretched for aspect ratio).
	for dx := -int(r * 2); dx <= int(r*2); dx++ {
		c.set(cx+dx, cy, '·', color)
	}
}

// Render draws the full radar display and returns it as a string.
func Render(width, height int, peers []protocol.PeerData) string {
	// Reserve bottom rows for the peer list legend.
	legendRows := len(peers) + 2
	radarH := height - legendRows
	if radarH < 10 {
		radarH = 10
	}

	c := newCanvas(width, height)

	cx := width / 2
	cy := radarH / 2

	// Radius in character rows — limited by available space.
	radius := float64(min(width/4, radarH/2) - 1)
	if radius < 4 {
		radius = 4
	}

	// Zone radii (proportional to max range).
	rRed := radius * (RedThreshold / MaxRange)
	rYellow := radius * (YellowThreshold / MaxRange)
	rOuter := radius

	// Draw from outside in so inner rings overwrite.
	c.drawCrosshair(cx, cy, rOuter, dim)
	c.drawCircle(cx, cy, rOuter, '·', green)
	c.drawCircle(cx, cy, rYellow, '·', yellow)
	c.drawCircle(cx, cy, rRed, '·', red)

	// Cardinal labels.
	c.setStr(cx-1, cy-int(rOuter)-1, "N", bold+cyan)
	c.setStr(cx-1, cy+int(rOuter)+1, "S", bold+cyan)
	c.setStr(cx-int(rOuter*2)-2, cy, "W", bold+cyan)
	c.setStr(cx+int(rOuter*2)+1, cy, "E", bold+cyan)

	// Center marker.
	c.setStr(cx-1, cy, "YOU", bold+white)

	// Zone distance labels.
	redLabel := fmt.Sprintf("%dm", int(RedThreshold))
	yellowLabel := fmt.Sprintf("%dm", int(YellowThreshold))
	outerLabel := fmt.Sprintf("%dm", int(MaxRange))
	c.setStr(cx+1, cy-int(rRed), redLabel, dim+red)
	c.setStr(cx+1, cy-int(rYellow), yellowLabel, dim+yellow)
	c.setStr(cx+1, cy-int(rOuter), outerLabel, dim+green)

	// Place peers.
	for _, p := range peers {
		normDist := p.Distance / MaxRange
		if normDist > 1 {
			normDist = 1
		}

		rad := p.Bearing * math.Pi / 180
		px := cx + int(math.Round(normDist*radius*math.Sin(rad)*2))
		py := cy - int(math.Round(normDist*radius*math.Cos(rad)))

		color := peerColor(p.Distance)
		c.set(px, py, '●', bold+color)

		// Short label next to the marker.
		label := fmt.Sprintf(" %.0fm", p.Distance)
		c.setStr(px+1, py, label, color)
	}

	// Legend at the bottom.
	legendY := radarH + 1
	c.setStr(0, legendY, "─── Peers ───", dim)
	for i, p := range peers {
		color := peerColor(p.Distance)
		stateIcon := stateIcon(p.State)
		line := fmt.Sprintf(" %s %-20s %6.1fm  %5.1f°  %s %s",
			stateIcon, p.MAC, p.Distance, p.Bearing, p.State, reset)
		c.setStr(0, legendY+1+i, line, color)
	}

	return c.render()
}

func peerColor(distance float64) string {
	switch {
	case distance < RedThreshold:
		return red
	case distance < YellowThreshold:
		return yellow
	default:
		return green
	}
}

func stateIcon(state string) string {
	switch state {
	case "BRAKING":
		return red + "▼" + reset
	case "ACCELERATING":
		return green + "▲" + reset
	default:
		return dim + "■" + reset
	}
}

func min(a, b int) int {
	if a < b {
		return a
	}
	return b
}
