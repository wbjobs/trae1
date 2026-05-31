package terminal

import ()

func (t *Terminal) Write(data []byte) (int, error) {
	t.mu.Lock()
	defer t.mu.Unlock()
	t.writeBytes(data)
	return len(data), nil
}

func (t *Terminal) writeBytes(data []byte) {
	i := 0
	for i < len(data) {
		b := data[i]
		switch b {
		case 0x07:
			i++
		case 0x08:
			if t.cursorX > 0 {
				t.cursorX--
			}
			i++
		case 0x09:
			t.writeTab()
			i++
		case 0x0A, 0x0B, 0x0C:
			t.writeLinefeed()
			i++
		case 0x0D:
			t.cursorX = 0
			i++
		case 0x1B:
			i = t.handleEscape(data, i+1)
		default:
			if b >= 0x20 && b != 0x7F {
				t.putChar(b)
			}
			i++
		}
	}
}

func (t *Terminal) handleEscape(data []byte, start int) int {
	if start >= len(data) {
		return start
	}

	switch data[start] {
	case '[':
		return t.parseCSI(data, start+1)
	case ']':
		return t.parseOSC(data, start+1)
	case '(':
		if start+1 < len(data) {
			return start + 2
		}
		return start + 1
	case ')':
		if start+1 < len(data) {
			return start + 2
		}
		return start + 1
	case 'D':
		t.writeLinefeed()
		return start + 1
	case 'E':
		t.cursorX = 0
		t.writeLinefeed()
		return start + 1
	case 'H':
		t.setTabStop()
		return start + 1
	case 'M':
		t.reverseIndex()
		return start + 1
	case 'N':
		if start+1 < len(data) {
			return start + 2
		}
		return start + 1
	case 'O':
		if start+1 < len(data) {
			return start + 2
		}
		return start + 1
	case 'P':
		return t.parseDCS(data, start+1)
	case 'c':
		t.reset()
		return start + 1
	case '7':
		t.saveCursor()
		return start + 1
	case '8':
		t.restoreCursor()
		return start + 1
	default:
		return start + 1
	}
}

func (t *Terminal) parseCSI(data []byte, start int) int {
	i := start
	var params []int
	currentParam := -1
	hasQuestion := false

	for i < len(data) {
		b := data[i]
		if b == '?' {
			hasQuestion = true
			i++
			continue
		}
		if b >= '0' && b <= '9' {
			if currentParam < 0 {
				currentParam = 0
			}
			currentParam = currentParam*10 + int(b-'0')
			i++
			continue
		}
		if b == ';' {
			if currentParam < 0 {
				params = append(params, 0)
			} else {
				params = append(params, currentParam)
			}
			currentParam = -1
			i++
			continue
		}
		if b >= 0x40 && b <= 0x7E {
			if currentParam >= 0 {
				params = append(params, currentParam)
			}
			if hasQuestion {
				t.handleCSIMode(b, params)
			} else {
				t.handleCSICommand(b, params)
			}
			return i + 1
		}
		i++
	}
	return i
}

func (t *Terminal) handleCSICommand(cmd byte, params []int) {
	switch cmd {
	case 'A':
		n := getParam(params, 0, 1)
		t.cursorY -= n
		if t.cursorY < 0 {
			t.cursorY = 0
		}
	case 'B':
		n := getParam(params, 0, 1)
		t.cursorY += n
		if t.cursorY >= t.height {
			t.cursorY = t.height - 1
		}
	case 'C':
		n := getParam(params, 0, 1)
		t.cursorX += n
		if t.cursorX >= t.width {
			t.cursorX = t.width - 1
		}
	case 'D':
		n := getParam(params, 0, 1)
		t.cursorX -= n
		if t.cursorX < 0 {
			t.cursorX = 0
		}
	case 'E':
		n := getParam(params, 0, 1)
		t.cursorX = 0
		t.cursorY += n
		if t.cursorY >= t.height {
			t.cursorY = t.height - 1
		}
	case 'F':
		n := getParam(params, 0, 1)
		t.cursorX = 0
		t.cursorY -= n
		if t.cursorY < 0 {
			t.cursorY = 0
		}
	case 'G':
		n := getParam(params, 0, 1)
		t.cursorX = n - 1
		if t.cursorX < 0 {
			t.cursorX = 0
		}
		if t.cursorX >= t.width {
			t.cursorX = t.width - 1
		}
	case 'H', 'f':
		if len(params) >= 2 {
			t.cursorY = params[0] - 1
			t.cursorX = params[1] - 1
		} else {
			t.cursorY = 0
			t.cursorX = 0
		}
		t.clampCursor()
	case 'J':
		mode := getParam(params, 0, 0)
		t.eraseInDisplay(mode)
	case 'K':
		mode := getParam(params, 0, 0)
		t.eraseInLine(mode)
	case 'L':
		n := getParam(params, 0, 1)
		t.insertLines(n)
	case 'M':
		n := getParam(params, 0, 1)
		t.deleteLines(n)
	case 'P':
		n := getParam(params, 0, 1)
		t.deleteChars(n)
	case '@':
		n := getParam(params, 0, 1)
		t.insertChars(n)
	case 'S':
		n := getParam(params, 0, 1)
		t.scrollUp(n)
	case 'T':
		n := getParam(params, 0, 1)
		t.scrollDown(n)
	case 'X':
		n := getParam(params, 0, 1)
		t.eraseChars(n)
	case 'd':
		n := getParam(params, 0, 1)
		t.cursorY = n - 1
		t.clampCursor()
	case 'm':
		t.handleSGR(params)
	case 'r':
		if len(params) >= 2 {
			t.scrollTop = params[0] - 1
			t.scrollBottom = params[1] - 1
		} else {
			t.scrollTop = 0
			t.scrollBottom = t.height - 1
		}
		if t.scrollTop < 0 {
			t.scrollTop = 0
		}
		if t.scrollBottom >= t.height {
			t.scrollBottom = t.height - 1
		}
		t.cursorX = 0
		t.cursorY = t.scrollTop
	case 's':
		t.saveCursor()
	case 'u':
		t.restoreCursor()
	default:
	}
}

func (t *Terminal) handleCSIMode(cmd byte, params []int) {
	switch cmd {
	case 'h':
		for _, p := range params {
			t.setMode(p)
		}
	case 'l':
		for _, p := range params {
			t.resetMode(p)
		}
	default:
	}
}

func (t *Terminal) setMode(mode int) {
	switch mode {
	case 1049, 47, 1047:
		t.switchToAltScreen()
	}
}

func (t *Terminal) resetMode(mode int) {
	switch mode {
	case 1049, 47, 1047:
		t.switchToMainScreen()
	}
}

func (t *Terminal) switchToAltScreen() {
	if t.useAlt {
		return
	}
	t.savedX = t.cursorX
	t.savedY = t.cursorY
	t.useAlt = true
	t.clearScreen()
	t.cursorX = 0
	t.cursorY = 0
}

func (t *Terminal) switchToMainScreen() {
	if !t.useAlt {
		return
	}
	t.useAlt = false
	t.clearScreen()
	t.cursorX = t.savedX
	t.cursorY = t.savedY
	t.clampCursor()
}

func (t *Terminal) handleSGR(params []int) {
	if len(params) == 0 {
		t.attr = 0
		t.fg = ColorDefault
		t.bg = ColorDefault
		return
	}
	for i := 0; i < len(params); i++ {
		p := params[i]
		switch {
		case p == 0:
			t.attr = 0
			t.fg = ColorDefault
			t.bg = ColorDefault
		case p == 1:
			t.attr |= AttrBold
		case p == 2:
			t.attr |= AttrDim
		case p == 3:
			t.attr |= AttrItalic
		case p == 4:
			t.attr |= AttrUnderline
		case p == 5:
			t.attr |= AttrBlink
		case p == 7:
			t.attr |= AttrReverse
		case p == 8:
			t.attr |= AttrHidden
		case p == 9:
			t.attr |= AttrStrike
		case p == 22:
			t.attr &^= (AttrBold | AttrDim)
		case p == 23:
			t.attr &^= AttrItalic
		case p == 24:
			t.attr &^= AttrUnderline
		case p == 25:
			t.attr &^= AttrBlink
		case p == 27:
			t.attr &^= AttrReverse
		case p == 28:
			t.attr &^= AttrHidden
		case p == 29:
			t.attr &^= AttrStrike
		case p >= 30 && p <= 37:
			t.fg = int16(p - 30)
		case p == 38:
			if i+2 < len(params) && params[i+1] == 5 {
				t.fg = int16(params[i+2])
				i += 2
			}
		case p == 39:
			t.fg = ColorDefault
		case p >= 40 && p <= 47:
			t.bg = int16(p - 40)
		case p == 48:
			if i+2 < len(params) && params[i+1] == 5 {
				t.bg = int16(params[i+2])
				i += 2
			}
		case p == 49:
			t.bg = ColorDefault
		case p >= 90 && p <= 97:
			t.fg = int16(p - 90)
		case p >= 100 && p <= 107:
			t.bg = int16(p - 100)
		}
	}
}

func (t *Terminal) putChar(b byte) {
	var buf []Cell
	if t.useAlt {
		buf = t.altCells
	} else {
		buf = t.cells
	}
	buf[t.cursorY*t.width+t.cursorX] = Cell{
		Char: b,
		Fg:   t.fg,
		Bg:   t.bg,
		Attr: t.attr,
	}
	t.cursorX++
	if t.cursorX >= t.width {
		t.cursorX = 0
		t.cursorY++
		if t.cursorY > t.scrollBottom {
			t.scrollUp(1)
			t.cursorY = t.scrollBottom
		}
	}
}

func (t *Terminal) writeTab() {
	for _, tab := range t.tabs {
		if tab > t.cursorX {
			t.cursorX = tab
			if t.cursorX >= t.width {
				t.cursorX = t.width - 1
			}
			return
		}
	}
	t.cursorX = t.width - 1
}

func (t *Terminal) setTabStop() {
	exists := false
	for _, tab := range t.tabs {
		if tab == t.cursorX {
			exists = true
			break
		}
	}
	if !exists {
		t.tabs = append(t.tabs, t.cursorX)
	}
}

func (t *Terminal) writeLinefeed() {
	if t.cursorY < t.scrollBottom {
		t.cursorY++
	} else {
		t.scrollUp(1)
	}
}

func (t *Terminal) reverseIndex() {
	if t.cursorY == t.scrollTop {
		t.scrollDown(1)
	} else {
		t.cursorY--
	}
}

func (t *Terminal) scrollUp(n int) {
	if n <= 0 || n > t.height {
		return
	}
	var buf []Cell
	if t.useAlt {
		buf = t.altCells
	} else {
		buf = t.cells
	}
	for y := t.scrollTop; y <= t.scrollBottom-n; y++ {
		for x := 0; x < t.width; x++ {
			buf[y*t.width+x] = buf[(y+n)*t.width+x]
		}
	}
	for y := t.scrollBottom - n + 1; y <= t.scrollBottom; y++ {
		for x := 0; x < t.width; x++ {
			buf[y*t.width+x] = Cell{}
		}
	}
}

func (t *Terminal) scrollDown(n int) {
	if n <= 0 || n > t.height {
		return
	}
	var buf []Cell
	if t.useAlt {
		buf = t.altCells
	} else {
		buf = t.cells
	}
	for y := t.scrollBottom; y >= t.scrollTop+n; y-- {
		for x := 0; x < t.width; x++ {
			buf[y*t.width+x] = buf[(y-n)*t.width+x]
		}
	}
	for y := t.scrollTop; y < t.scrollTop+n; y++ {
		for x := 0; x < t.width; x++ {
			buf[y*t.width+x] = Cell{}
		}
	}
}

func (t *Terminal) eraseInDisplay(mode int) {
	var buf []Cell
	if t.useAlt {
		buf = t.altCells
	} else {
		buf = t.cells
	}
	switch mode {
	case 0:
		idx := t.cursorY*t.width + t.cursorX
		for i := idx; i < len(buf); i++ {
			buf[i] = Cell{}
		}
	case 1:
		idx := t.cursorY*t.width + t.cursorX + 1
		for i := 0; i < idx && i < len(buf); i++ {
			buf[i] = Cell{}
		}
	case 2, 3:
		for i := range buf {
			buf[i] = Cell{}
		}
	}
}

func (t *Terminal) eraseInLine(mode int) {
	var buf []Cell
	if t.useAlt {
		buf = t.altCells
	} else {
		buf = t.cells
	}
	switch mode {
	case 0:
		for x := t.cursorX; x < t.width; x++ {
			buf[t.cursorY*t.width+x] = Cell{}
		}
	case 1:
		for x := 0; x <= t.cursorX; x++ {
			buf[t.cursorY*t.width+x] = Cell{}
		}
	case 2:
		for x := 0; x < t.width; x++ {
			buf[t.cursorY*t.width+x] = Cell{}
		}
	}
}

func (t *Terminal) eraseChars(n int) {
	var buf []Cell
	if t.useAlt {
		buf = t.altCells
	} else {
		buf = t.cells
	}
	end := t.cursorX + n
	if end > t.width {
		end = t.width
	}
	for x := t.cursorX; x < end; x++ {
		buf[t.cursorY*t.width+x] = Cell{}
	}
}

func (t *Terminal) insertLines(n int) {
	if n <= 0 || n > t.height {
		return
	}
	var buf []Cell
	if t.useAlt {
		buf = t.altCells
	} else {
		buf = t.cells
	}
	for y := t.scrollBottom; y >= t.cursorY+n; y-- {
		for x := 0; x < t.width; x++ {
			buf[y*t.width+x] = buf[(y-n)*t.width+x]
		}
	}
	for y := t.cursorY; y < t.cursorY+n && y <= t.scrollBottom; y++ {
		for x := 0; x < t.width; x++ {
			buf[y*t.width+x] = Cell{}
		}
	}
}

func (t *Terminal) deleteLines(n int) {
	if n <= 0 || n > t.height {
		return
	}
	var buf []Cell
	if t.useAlt {
		buf = t.altCells
	} else {
		buf = t.cells
	}
	for y := t.cursorY; y <= t.scrollBottom-n; y++ {
		for x := 0; x < t.width; x++ {
			buf[y*t.width+x] = buf[(y+n)*t.width+x]
		}
	}
	for y := t.scrollBottom - n + 1; y <= t.scrollBottom; y++ {
		for x := 0; x < t.width; x++ {
			buf[y*t.width+x] = Cell{}
		}
	}
}

func (t *Terminal) insertChars(n int) {
	if n <= 0 || n > t.width {
		return
	}
	var buf []Cell
	if t.useAlt {
		buf = t.altCells
	} else {
		buf = t.cells
	}
	end := t.width - n
	if end < t.cursorX {
		return
	}
	for x := t.width - 1; x >= t.cursorX+n; x-- {
		buf[t.cursorY*t.width+x] = buf[t.cursorY*t.width+x-n]
	}
	for x := t.cursorX; x < t.cursorX+n; x++ {
		buf[t.cursorY*t.width+x] = Cell{}
	}
}

func (t *Terminal) deleteChars(n int) {
	if n <= 0 || n > t.width {
		return
	}
	var buf []Cell
	if t.useAlt {
		buf = t.altCells
	} else {
		buf = t.cells
	}
	end := t.width - n
	if end < t.cursorX {
		return
	}
	for x := t.cursorX; x < end; x++ {
		buf[t.cursorY*t.width+x] = buf[t.cursorY*t.width+x+n]
	}
	for x := end; x < t.width; x++ {
		buf[t.cursorY*t.width+x] = Cell{}
	}
}

func (t *Terminal) clearScreen() {
	for i := range t.cells {
		t.cells[i] = Cell{}
	}
}

func (t *Terminal) reset() {
	t.cursorX = 0
	t.cursorY = 0
	t.scrollTop = 0
	t.scrollBottom = t.height - 1
	t.attr = 0
	t.fg = ColorDefault
	t.bg = ColorDefault
	t.useAlt = false
	t.clearScreen()
}

func (t *Terminal) saveCursor() {
	if t.useAlt {
		t.altSavedX = t.cursorX
		t.altSavedY = t.cursorY
	} else {
		t.savedX = t.cursorX
		t.savedY = t.cursorY
	}
}

func (t *Terminal) restoreCursor() {
	if t.useAlt {
		t.cursorX = t.altSavedX
		t.cursorY = t.altSavedY
	} else {
		t.cursorX = t.savedX
		t.cursorY = t.savedY
	}
	t.clampCursor()
}

func (t *Terminal) clampCursor() {
	if t.cursorX < 0 {
		t.cursorX = 0
	}
	if t.cursorX >= t.width {
		t.cursorX = t.width - 1
	}
	if t.cursorY < 0 {
		t.cursorY = 0
	}
	if t.cursorY >= t.height {
		t.cursorY = t.height - 1
	}
}

func (t *Terminal) parseOSC(data []byte, start int) int {
	i := start
	for i < len(data) {
		if data[i] == 0x07 || (data[i] == 0x1B && i+1 < len(data) && data[i+1] == '\\') {
			return i + 1
		}
		i++
	}
	return i
}

func (t *Terminal) parseDCS(data []byte, start int) int {
	i := start
	for i < len(data) {
		if data[i] == 0x1B && i+1 < len(data) && data[i+1] == '\\' {
			return i + 2
		}
		i++
	}
	return i
}

func getParam(params []int, idx, def int) int {
	if idx < len(params) {
		v := params[idx]
		if v == 0 {
			return def
		}
		return v
	}
	return def
}

func (f *Frame) RenderText() string {
	var result []byte
	for y := 0; y < f.Height; y++ {
		rowStart := y * f.Width
		lastNonSpace := -1
		for x := f.Width - 1; x >= 0; x-- {
			if f.Cells[rowStart+x].Char != 0 && f.Cells[rowStart+x].Char != ' ' {
				lastNonSpace = x
				break
			}
		}
		for x := 0; x <= lastNonSpace; x++ {
			ch := f.Cells[rowStart+x].Char
			if ch == 0 {
				ch = ' '
			}
			result = append(result, ch)
		}
		result = append(result, '\n')
	}
	return string(result)
}

func (f *Frame) RenderLine(y int) string {
	if y < 0 || y >= f.Height {
		return ""
	}
	rowStart := y * f.Width
	var result []byte
	for x := 0; x < f.Width; x++ {
		ch := f.Cells[rowStart+x].Char
		if ch == 0 {
			ch = ' '
		}
		result = append(result, ch)
	}
	return string(result)
}

func (f *Frame) Contains(text string) (int, int, bool) {
	target := []byte(text)
	for y := 0; y < f.Height; y++ {
		for x := 0; x < f.Width; x++ {
			if matchAt(f.Cells, f.Width, f.Height, x, y, target) {
				return x, y, true
			}
		}
	}
	return -1, -1, false
}

func (f *Frame) HighlightMatches(text string) []Match {
	var matches []Match
	target := []byte(text)
	if len(target) == 0 {
		return matches
	}
	for y := 0; y < f.Height; y++ {
		for x := 0; x < f.Width; x++ {
			if matchAt(f.Cells, f.Width, f.Height, x, y, target) {
				matches = append(matches, Match{X: x, Y: y, Length: len(target)})
			}
		}
	}
	return matches
}

type Match struct {
	X      int
	Y      int
	Length int
}

func FrameFromTimestamp(frames []Frame, ts float64) *Frame {
	if len(frames) == 0 {
		return nil
	}
	idx := 0
	for i, f := range frames {
		if f.Timestamp <= ts {
			idx = i
		} else {
			break
		}
	}
	if frames[idx].Timestamp > ts && idx > 0 {
		idx--
	}
	return &frames[idx]
}

func SearchFrames(frames []Frame, query string, fromTs float64, maxResults int) []SearchResult {
	var results []SearchResult
	count := 0
	for _, f := range frames {
		if f.Timestamp < fromTs {
			continue
		}
		matches := f.HighlightMatches(query)
		if len(matches) > 0 {
			for _, m := range matches {
				results = append(results, SearchResult{
					Timestamp: f.Timestamp,
					Match:     m,
					Line:      f.RenderLine(m.Y),
					LineNum:   m.Y,
				})
				count++
				if maxResults > 0 && count >= maxResults {
					return results
				}
			}
		}
	}
	return results
}

type SearchResult struct {
	Timestamp float64 `json:"timestamp"`
	Match     Match   `json:"match"`
	Line      string  `json:"line"`
	LineNum   int     `json:"line_num"`
}

func (t *Terminal) GetActiveBuffer() []Cell {
	t.mu.RLock()
	defer t.mu.RUnlock()
	if t.useAlt {
		result := make([]Cell, len(t.altCells))
		copy(result, t.altCells)
		return result
	}
	result := make([]Cell, len(t.cells))
	copy(result, t.cells)
	return result
}
