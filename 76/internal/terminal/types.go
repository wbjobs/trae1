package terminal

import "sync"

type Cell struct {
	Char byte
	Fg   int16
	Bg   int16
	Attr uint16
}

const (
	AttrBold      uint16 = 1
	AttrDim       uint16 = 2
	AttrItalic    uint16 = 4
	AttrUnderline uint16 = 8
	AttrBlink     uint16 = 16
	AttrReverse   uint16 = 32
	AttrHidden    uint16 = 64
	AttrStrike    uint16 = 128
)

const (
	ColorDefault int16 = -1
	ColorBlack   int16 = 0
	ColorRed     int16 = 1
	ColorGreen   int16 = 2
	ColorYellow  int16 = 3
	ColorBlue    int16 = 4
	ColorMagenta int16 = 5
	ColorCyan    int16 = 6
	ColorWhite   int16 = 7
)

type Frame struct {
	Timestamp float64
	Width     int
	Height    int
	Cells     []Cell
}

type Terminal struct {
	width    int
	height   int
	cells    []Cell
	altCells []Cell

	cursorX int
	cursorY int
	scrollTop int
	scrollBottom int

	savedX int
	savedY int
	altSavedX int
	altSavedY int

	fg       int16
	bg       int16
	attr     uint16
	useAlt   bool

	tabs []int

	mu sync.RWMutex
}

func NewTerminal(width, height int) *Terminal {
	t := &Terminal{
		width:       width,
		height:      height,
		cursorX:     0,
		cursorY:     0,
		scrollTop:   0,
		scrollBottom: height - 1,
		fg:          ColorDefault,
		bg:          ColorDefault,
	}
	t.cells = make([]Cell, width*height)
	t.altCells = make([]Cell, width*height)
	t.initTabs()
	return t
}

func (t *Terminal) initTabs() {
	t.tabs = make([]int, 0)
	for i := 0; i < t.width; i += 8 {
		t.tabs = append(t.tabs, i)
	}
}

func (t *Terminal) Resize(width, height int) {
	t.mu.Lock()
	defer t.mu.Unlock()

	newCells := make([]Cell, width*height)
	newAltCells := make([]Cell, width*height)

	minW := t.width
	if width < minW {
		minW = width
	}
	minH := t.height
	if height < minH {
		minH = height
	}

	for y := 0; y < minH; y++ {
		for x := 0; x < minW; x++ {
			newCells[y*width+x] = t.cells[y*t.width+x]
			newAltCells[y*width+x] = t.altCells[y*t.width+x]
		}
	}

	t.width = width
	t.height = height
	t.cells = newCells
	t.altCells = newAltCells
	t.scrollBottom = height - 1

	if t.cursorX >= width {
		t.cursorX = width - 1
	}
	if t.cursorY >= height {
		t.cursorY = height - 1
	}
	t.initTabs()
}

func (t *Terminal) Width() int { return t.width }
func (t *Terminal) Height() int { return t.height }

func (t *Terminal) CursorPos() (int, int) {
	t.mu.RLock()
	defer t.mu.RUnlock()
	return t.cursorX, t.cursorY
}

func (t *Terminal) RenderText() string {
	t.mu.RLock()
	defer t.mu.RUnlock()

	var result []byte
	for y := 0; y < t.height; y++ {
		rowStart := y * t.width
		lastNonSpace := -1
		for x := t.width - 1; x >= 0; x-- {
			if t.cells[rowStart+x].Char != 0 && t.cells[rowStart+x].Char != ' ' {
				lastNonSpace = x
				break
			}
		}
		for x := 0; x <= lastNonSpace; x++ {
			ch := t.cells[rowStart+x].Char
			if ch == 0 {
				ch = ' '
			}
			result = append(result, ch)
		}
		result = append(result, '\n')
	}
	return string(result)
}

func (t *Terminal) RenderLine(y int) string {
	t.mu.RLock()
	defer t.mu.RUnlock()

	if y < 0 || y >= t.height {
		return ""
	}
	rowStart := y * t.width
	var result []byte
	for x := 0; x < t.width; x++ {
		ch := t.cells[rowStart+x].Char
		if ch == 0 {
			ch = ' '
		}
		result = append(result, ch)
	}
	return string(result)
}

func (t *Terminal) RenderLineTrimmed(y int) string {
	t.mu.RLock()
	defer t.mu.RUnlock()

	if y < 0 || y >= t.height {
		return ""
	}
	rowStart := y * t.width
	lastNonSpace := -1
	for x := t.width - 1; x >= 0; x-- {
		if t.cells[rowStart+x].Char != 0 && t.cells[rowStart+x].Char != ' ' {
			lastNonSpace = x
			break
		}
	}
	if lastNonSpace < 0 {
		return ""
	}
	var result []byte
	for x := 0; x <= lastNonSpace; x++ {
		ch := t.cells[rowStart+x].Char
		if ch == 0 {
			ch = ' '
		}
		result = append(result, ch)
	}
	return string(result)
}

func (t *Terminal) Snapshot() Frame {
	t.mu.RLock()
	defer t.mu.RUnlock()

	var buf []Cell
	if t.useAlt {
		buf = t.altCells
	} else {
		buf = t.cells
	}
	cells := make([]Cell, len(buf))
	copy(cells, buf)
	return Frame{
		Width:  t.width,
		Height: t.height,
		Cells:  cells,
	}
}

func (t *Terminal) Contains(text string) (int, int, bool) {
	t.mu.RLock()
	defer t.mu.RUnlock()

	if len(text) == 0 {
		return -1, -1, false
	}

	var buf []Cell
	if t.useAlt {
		buf = t.altCells
	} else {
		buf = t.cells
	}

	target := []byte(text)
	for y := 0; y < t.height; y++ {
		for x := 0; x < t.width; x++ {
			if matchAt(buf, t.width, t.height, x, y, target) {
				return x, y, true
			}
		}
	}
	return -1, -1, false
}

func matchAt(cells []Cell, width, height int, x, y int, target []byte) bool {
	if x+len(target) > width {
		remaining := width - x
		for i := 0; i < remaining && i < len(target); i++ {
			ch := cells[y*width+x+i].Char
			if ch == 0 {
				ch = ' '
			}
			if ch != target[i] {
				return false
			}
		}
		offset := remaining
		target = target[offset:]
		y++
		x = 0
		if y >= height {
			return false
		}
	}

	remaining := len(target)
	for i := 0; i < remaining; i++ {
		col := x + i
		row := y
		for col >= width {
			col -= width
			row++
			if row >= height {
				return false
			}
		}
		ch := cells[row*width+col].Char
		if ch == 0 {
			ch = ' '
		}
		if ch != target[i] {
			return false
		}
	}
	return true
}
