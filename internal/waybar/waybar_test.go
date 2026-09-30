package waybar

import (
	"encoding/json"
	"testing"
)

func TestWaybarFormat(t *testing.T) {
	// Test running state
	jsonStr := Format("work", 1500, 1500, true)
	var out Output
	if err := json.Unmarshal([]byte(jsonStr), &out); err != nil {
		t.Fatalf("Failed to unmarshal waybar JSON: %v", err)
	}

	if out.Text != "<span color=\"#a6e3a1\">25:00</span>" {
		t.Errorf("Expected Text '<span color=\"#a6e3a1\">25:00</span>', got %q", out.Text)
	}
	if out.Alt != "work-running" {
		t.Errorf("Expected Alt 'work-running', got %q", out.Alt)
	}
	if out.Class != "work-running running" {
		t.Errorf("Expected Class 'work-running running', got %q", out.Class)
	}
	if !out.Running {
		t.Errorf("Expected Running true, got false")
	}

	// Test stopped/paused state
	jsonStrPaused := Format("work", 1500, 1500, false)
	var outPaused Output
	if err := json.Unmarshal([]byte(jsonStrPaused), &outPaused); err != nil {
		t.Fatalf("Failed to unmarshal waybar JSON: %v", err)
	}

	if outPaused.Text != "25:00" {
		t.Errorf("Expected Text '25:00', got %q", outPaused.Text)
	}

	if outPaused.Alt != "work" {
		t.Errorf("Expected Alt 'work', got %q", outPaused.Alt)
	}
	if outPaused.Class != "work-stopped stopped" {
		t.Errorf("Expected Class 'work-stopped stopped', got %q", outPaused.Class)
	}
	if outPaused.Running {
		t.Errorf("Expected Running false, got true")
	}
}
