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

	statusClass := "stopped"
	if running {
		statusClass = "running"
	}

	out := Output{
		Text:      fmt.Sprintf("%02d:%02d", mins, secs), // Pure time text, no icons
		Alt:       mode,                                 // Used by Waybar to select format-icons
		Tooltip:   fmt.Sprintf("Mode: %s | Status: %s", mode, statusClass),
		Class:     fmt.Sprintf("%s-%s", mode, statusClass),
		Mode:      mode,
		Remaining: remainingSec,
		Total:     totalSec,
		Running:   running,
	}

	bytes, _ := json.Marshal(out)
	return string(bytes)
}
