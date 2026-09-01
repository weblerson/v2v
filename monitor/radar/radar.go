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
	// Advance by rune, not by byte. Ranging over a string yields byte offsets,
	// so a multi-byte rune used to leave holes in the canvas and push the rest
	// of the line out of column — visible in the "─── Peers ───" header and in
	// any row containing "°" or "—".
	col := 0
	for _, ch := range s {
		c.set(x+col, y, ch, color)
		col++
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

// drawRing plots a peer's distance ring: the honest rendering for a peer whose
// distance is known but whose direction is not. A minimum radius keeps very
// close peers visible instead of collapsing them onto the centre marker.
func (c *canvas) drawRing(cx, cy int, r float64, color string) {
	if r < 1.5 {
		r = 1.5
	}
	steps := int(2 * math.Pi * r * 3)
	if steps < 60 {
		steps = 60
	}
	for i := 0; i < steps; i++ {
		angle := 2 * math.Pi * float64(i) / float64(steps)
		x := cx + int(math.Round(r*math.Sin(angle)*2))
		y := cy - int(math.Round(r*math.Cos(angle)))
		c.set(x, y, '○', color)
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

// radialFraction maps a distance to its position along the radar's radius,
// from 0 at the centre to 1 at MaxRange.
//
// Square root rather than linear. On a linear scale with a 50 m outer edge the
// 5 m danger zone occupies only a tenth of the radius, crushing every nearby
// vehicle into a couple of characters around the centre — exactly the range
// where UWB ranging is most accurate and where the driver most needs to read
// the display. Under square root that zone gets about a third of the radius.
func radialFraction(d float64) float64 {
	if d <= 0 {
		return 0
	}
	f := math.Sqrt(d / MaxRange)
	if f > 1 {
		f = 1
	}
	return f
}

// Render draws the full radar display and returns it as a string.
func Render(width, height int, peers []protocol.PeerData) string {
	// Reserve bottom rows for the peer list legend (header + column titles).
	legendRows := len(peers) + 3
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

	// Zone radii, on the same scale the peers use.
	rRed := radius * radialFraction(RedThreshold)
	rYellow := radius * radialFraction(YellowThreshold)
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
		normDist := radialFraction(p.Distance)
		color := peerColor(p.Distance)
		label := fmt.Sprintf("%.1fm", p.Distance)

		if !p.BearingValid {
			// Distance known, direction unknown — the normal case on the UWB
			// backend. A ring says "somewhere at this radius"; a dot would
			// claim a direction the hardware never measured.
			r := normDist * radius
			c.drawRing(cx, cy, r, color)
			if r < 1.5 {
				r = 1.5
			}
			c.setStr(cx+2, cy-int(math.Round(r)), label, bold+color)
			continue
		}

		rad := p.Bearing * math.Pi / 180
		px := cx + int(math.Round(normDist*radius*math.Sin(rad)*2))
		py := cy - int(math.Round(normDist*radius*math.Cos(rad)))

		c.set(px, py, '●', bold+color)
		c.setStr(px+1, py, " "+label, color)
	}

	// Legend at the bottom.
	legendY := radarH + 1
	c.setStr(0, legendY, "─── Peers ───", dim)
	c.setStr(0, legendY+1,
		fmt.Sprintf("   %-18s %8s  %7s  %8s  %6s  %s",
			"MAC", "DIST", "BEARING", "CLOSING", "TTC", "STATE"), dim)

	for i, p := range peers {
		color := peerColor(p.Distance)

		bearing := "     —"
		if p.BearingValid {
			bearing = fmt.Sprintf("%5.1f°", p.Bearing)
		}
		ttc := "     —"
		if p.TTC >= 0 {
			ttc = fmt.Sprintf("%5.1fs", p.TTC)
		}

		line := fmt.Sprintf(" %s %-18s %7.2fm  %7s  %+6.2fm/s  %6s  %s",
			stateIcon(p.State), p.MAC, p.Distance, bearing, p.Closing, ttc, p.State)
		c.setStr(0, legendY+2+i, line, color)
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
