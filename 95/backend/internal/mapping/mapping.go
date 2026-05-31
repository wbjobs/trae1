package mapping

type Layout string

const (
	LayoutAuto   Layout = "auto"
	LayoutXbox   Layout = "xbox"
	LayoutPS5    Layout = "ps5"
	LayoutSwitch Layout = "switch"
)

type Mapping struct {
	Name    string   `json:"name"`
	Buttons []int    `json:"buttons"`
	Axes    []int    `json:"axes"`
	Invert  []bool   `json:"invert"`
}

var StandardGamepad = map[Layout]Mapping{
	LayoutXbox: {
		Name:    "Xbox",
		Buttons: []int{0, 1, 2, 3, 9, 8, 4, 5, 6, 7, 10, 11, 12, 13, 14, 15, 16},
		Axes:    []int{0, 1, 2, 3, 4, 5},
		Invert:  []bool{false, false, false, false, false, false},
	},
	LayoutPS5: {
		Name:    "PS5",
		Buttons: []int{2, 3, 1, 0, 9, 8, 4, 5, 6, 7, 10, 11, 12, 13, 14, 15, 16},
		Axes:    []int{0, 1, 3, 2, 4, 5},
		Invert:  []bool{false, false, false, true, false, false},
	},
	LayoutSwitch: {
		Name:    "Switch Pro",
		Buttons: []int{1, 0, 3, 2, 9, 8, 5, 4, 7, 6, 10, 11, 12, 13, 14, 15, 16},
		Axes:    []int{0, 1, 2, 3, 5, 4},
		Invert:  []bool{false, false, false, false, false, false},
	},
}

func Remap(srcButtons []bool, srcAxes []float32, layout Layout) (dstButtons [17]bool, dstAxes [6]float32) {
	m, ok := StandardGamepad[layout]
	if !ok {
		m = StandardGamepad[LayoutXbox]
	}
	for i, idx := range m.Buttons {
		if i >= len(dstButtons) {
			break
		}
		if idx < len(srcButtons) {
			dstButtons[i] = srcButtons[idx]
		}
	}
	for i, idx := range m.Axes {
		if i >= len(dstAxes) {
			break
		}
		if idx < len(srcAxes) {
			v := srcAxes[idx]
			if i < len(m.Invert) && m.Invert[i] {
				v = -v
			}
			dstAxes[i] = v
		}
	}
	return
}

func DetectLayout(id string) Layout {
	lower := toLower(id)
	if contains(lower, "045e") || contains(lower, "xbox") {
		return LayoutXbox
	}
	if contains(lower, "054c") || contains(lower, "054c:0ce6") || contains(lower, "dualshock") || contains(lower, "dualsense") {
		return LayoutPS5
	}
	if contains(lower, "057e") || contains(lower, "switch") || contains(lower, "pro controller") {
		return LayoutSwitch
	}
	return LayoutXbox
}

func toLower(s string) string {
	b := make([]byte, len(s))
	for i := 0; i < len(s); i++ {
		c := s[i]
		if c >= 'A' && c <= 'Z' {
			c += 32
		}
		b[i] = c
	}
	return string(b)
}

func contains(s, sub string) bool {
	if len(sub) == 0 {
		return true
	}
	for i := 0; i+len(sub) <= len(s); i++ {
		if s[i:i+len(sub)] == sub {
			return true
		}
	}
	return false
}
