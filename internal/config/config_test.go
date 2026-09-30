package config

import (
	"os"
	"testing"
)

func TestConfigLoadAndSave(t *testing.T) {
	tmpDir, err := os.MkdirTemp("", "waybar-pomodoro-cfg-test")
	if err != nil {
		t.Fatalf("Failed to create temp dir: %v", err)
	}
	defer os.RemoveAll(tmpDir)

	t.Setenv("XDG_CONFIG_HOME", tmpDir)

	cfg := Load()
	if cfg.WorkDurationMinutes != 25 || cfg.BreakDurationMinutes != 5 || cfg.LongBreakDurationMinutes != 15 || cfg.TotalCycles != 4 {
		t.Fatalf("Unexpected default config: %+v", cfg)
	}

	cfg.WorkDurationMinutes = 50
	cfg.BreakDurationMinutes = 10
	cfg.LongBreakDurationMinutes = 30
	cfg.TotalCycles = 3

	if err := Save(cfg); err != nil {
		t.Fatalf("Save config failed: %v", err)
	}

	loaded := Load()
	if loaded.WorkDurationMinutes != 50 || loaded.BreakDurationMinutes != 10 || loaded.LongBreakDurationMinutes != 30 || loaded.TotalCycles != 3 {
		t.Fatalf("Unexpected loaded config: %+v", loaded)
	}
}
