package config

import (
	"encoding/json"
	"os"
	"path/filepath"
)

type Config struct {
	WorkDurationMinutes      int `json:"work_duration_minutes"`
	BreakDurationMinutes     int `json:"break_duration_minutes"`
	LongBreakDurationMinutes int `json:"long_break_duration_minutes"`
	TotalCycles              int `json:"total_cycles"`
}

func DefaultConfig() Config {
	return Config{
		WorkDurationMinutes:      25,
		BreakDurationMinutes:     5,
		LongBreakDurationMinutes: 15,
		TotalCycles:              4,
	}
}

func getConfigPath() (string, error) {
	configDir, err := os.UserConfigDir()
	if err != nil {
		home, err := os.UserHomeDir()
		if err != nil {
			return "", err
		}
		configDir = filepath.Join(home, ".config")
	}
	dir := filepath.Join(configDir, "waybar-pomodoro")
	if err := os.MkdirAll(dir, 0o755); err != nil {
		return "", err
	}
	return filepath.Join(dir, "config.json"), nil
}

func Load() Config {
	cfg := DefaultConfig()
	path, err := getConfigPath()
	if err != nil {
		return cfg
	}
	data, err := os.ReadFile(path)
	if err != nil {
		return cfg
	}
	_ = json.Unmarshal(data, &cfg)
	if cfg.WorkDurationMinutes <= 0 {
		cfg.WorkDurationMinutes = 25
	}
	if cfg.BreakDurationMinutes <= 0 {
		cfg.BreakDurationMinutes = 5
	}
	if cfg.LongBreakDurationMinutes <= 0 {
		cfg.LongBreakDurationMinutes = 15
	}
	if cfg.TotalCycles <= 0 {
		cfg.TotalCycles = 4
	}
	return cfg
}

func Save(cfg Config) error {
	if cfg.WorkDurationMinutes <= 0 {
		cfg.WorkDurationMinutes = 25
	}
	if cfg.BreakDurationMinutes <= 0 {
		cfg.BreakDurationMinutes = 5
	}
	if cfg.LongBreakDurationMinutes <= 0 {
		cfg.LongBreakDurationMinutes = 15
	}
	if cfg.TotalCycles <= 0 {
		cfg.TotalCycles = 4
	}
	path, err := getConfigPath()
	if err != nil {
		return err
	}
	data, err := json.MarshalIndent(cfg, "", "  ")
	if err != nil {
		return err
	}
	return os.WriteFile(path, data, 0o644)
}
