package stats

import (
	"bufio"
	"encoding/json"
	"fmt"
	"os"
	"path/filepath"
	"time"
)

type SessionRecord struct {
	Timestamp time.Time `json:"timestamp"`
	Mode      string    `json:"mode"`
	Duration  int       `json:"duration_seconds"`
}

type StatsSummary struct {
	TodayCount    int
	TodayMinutes  int
	TotalCount    int
	TotalMinutes  int
	RecentHistory []SessionRecord
}

func getStatsFilePath() (string, error) {
	home, err := os.UserHomeDir()
	if err != nil {
		return "", err
	}
	dir := filepath.Join(home, ".local", "share", "waybar-pomodoro")
	if err := os.MkdirAll(dir, 0o755); err != nil {
		return "", err
	}
	return filepath.Join(dir, "stats.jsonl"), nil
}

func LogSession(mode string, durationSeconds int) error {
	filePath, err := getStatsFilePath()
	if err != nil {
		return err
	}

	file, err := os.OpenFile(filePath, os.O_APPEND|os.O_CREATE|os.O_WRONLY, 0o644)
	if err != nil {
		return err
	}
	defer file.Close()

	rec := SessionRecord{
		Timestamp: time.Now(),
		Mode:      mode,
		Duration:  durationSeconds,
	}

	bytes, _ := json.Marshal(rec)
	_, err = file.Write(append(bytes, '\n'))
	return err
}

func GetStats() (StatsSummary, error) {
	filePath, err := getStatsFilePath()
	if err != nil {
		return StatsSummary{}, err
	}

	file, err := os.Open(filePath)
	if os.IsNotExist(err) {
		return StatsSummary{}, nil
	} else if err != nil {
		return StatsSummary{}, err
	}
	defer file.Close()

	scanner := bufio.NewScanner(file)
	today := time.Now().Format("2006-01-02")
	var summary StatsSummary
	var allRecords []SessionRecord

	for scanner.Scan() {
		var rec SessionRecord
		if err := json.Unmarshal(scanner.Bytes(), &rec); err == nil {
			if rec.Mode == "work" {
				summary.TotalCount++
				summary.TotalMinutes += rec.Duration / 60
				if rec.Timestamp.Local().Format("2006-01-02") == today {
					summary.TodayCount++
					summary.TodayMinutes += rec.Duration / 60
				}
			}
			allRecords = append(allRecords, rec)
		}
	}

	// Last 5 sessions (newest first)
	for i := len(allRecords) - 1; i >= 0 && len(summary.RecentHistory) < 5; i-- {
		summary.RecentHistory = append(summary.RecentHistory, allRecords[i])
	}

	return summary, nil
}

func ShowStats() error {
	summary, err := GetStats()
	if err != nil {
		return err
	}

	today := time.Now().Format("2006-01-02")
	if summary.TotalCount == 0 {
		fmt.Println("No recorded sessions found yet.")
		return nil
	}

	fmt.Printf("📊 Pomodoro Summary for Today (%s):\n", today)
	fmt.Printf("   Completed Sessions: %d 🍅\n", summary.TodayCount)
	fmt.Printf("   Total Focus Time:   %d minutes\n", summary.TodayMinutes)
	return nil
}
