package waybar

import (
	"encoding/json"
	"fmt"
)

type Output struct {
	Text      string `json:"text"`
	Alt       string `json:"alt"`
	Tooltip   string `json:"tooltip"`
	Class     string `json:"class"`
	Mode      string `json:"mode,omitempty"`
	Remaining int    `json:"remaining,omitempty"`
	Total     int    `json:"total,omitempty"`
	Running   bool   `json:"running"`
}

func Format(mode string, remainingSec int, totalSec int, running bool) string {
	mins := remainingSec / 60
	secs := remainingSec % 60

	altMode := mode
	if mode == "long_break" {
		altMode = "break"
	}

	statusClass := "stopped"
	if running {
		statusClass = "running"
		altMode = fmt.Sprintf("%s-running", altMode)
	}

	text := fmt.Sprintf("%02d:%02d", mins, secs)
	if running {
		text = fmt.Sprintf("<span color=\"#a6e3a1\">%02d:%02d</span>", mins, secs)
	}

	out := Output{
		Text:      text,
		Alt:       altMode,
		Tooltip:   fmt.Sprintf("Mode: %s | Status: %s", mode, statusClass),
		Class:     fmt.Sprintf("%s-%s %s", mode, statusClass, statusClass),
		Mode:      mode,
		Remaining: remainingSec,
		Total:     totalSec,
		Running:   running,
	}

	bytes, _ := json.Marshal(out)
	return string(bytes)
}
