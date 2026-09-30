package stats

import (
	"os"
	"path/filepath"
	"testing"
)

func TestStatsLoggingAndDeletion(t *testing.T) {
	// Create a temporary directory for stats test
	tmpDir, err := os.MkdirTemp("", "waybar-pomodoro-stats-test")
	if err != nil {
		t.Fatalf("Failed to create temp dir: %v", err)
	}
	defer os.RemoveAll(tmpDir)

	// Set UserHomeDir to tmpDir for the duration of the test
	t.Setenv("HOME", tmpDir)

	statsFile := filepath.Join(tmpDir, ".local", "share", "waybar-pomodoro", "stats.jsonl")

	// 1. Initial stats should be empty
	summary, err := GetStats()
	if err != nil {
		t.Fatalf("GetStats failed: %v", err)
	}
	if summary.TodayCount != 0 || len(summary.TodayBlocks) != 0 {
		t.Fatalf("Expected 0 today blocks, got %d", summary.TodayCount)
	}

	// 2. Log two work sessions and one break session
	if err := LogSession("work", 1500); err != nil {
		t.Fatalf("LogSession work 1 failed: %v", err)
	}
	if err := LogSession("break", 300); err != nil {
		t.Fatalf("LogSession break failed: %v", err)
	}
	if err := LogSession("work", 1800); err != nil {
		t.Fatalf("LogSession work 2 failed: %v", err)
	}

	// Check file exists
	if _, err := os.Stat(statsFile); os.IsNotExist(err) {
		t.Fatalf("Stats file not created: %s", statsFile)
	}

	// 3. Verify GetStats
	summary, err = GetStats()
	if err != nil {
		t.Fatalf("GetStats failed: %v", err)
	}
	if summary.TodayCount != 2 {
		t.Fatalf("Expected TodayCount 2, got %d", summary.TodayCount)
	}
	if summary.TodayMinutes != 55 { // 25 + 30 = 55
		t.Fatalf("Expected TodayMinutes 55, got %d", summary.TodayMinutes)
	}
	if len(summary.TodayBlocks) != 2 {
		t.Fatalf("Expected 2 TodayBlocks, got %d", len(summary.TodayBlocks))
	}
	if summary.TodayBlocks[0].Index != 0 || summary.TodayBlocks[0].Duration != 25 {
		t.Errorf("Block 0 mismatch: %+v", summary.TodayBlocks[0])
	}
	if summary.TodayBlocks[1].Index != 1 || summary.TodayBlocks[1].Duration != 30 {
		t.Errorf("Block 1 mismatch: %+v", summary.TodayBlocks[1])
	}

	// 4. Delete today block index 0 (the 25 min session)
	if err := DeleteTodayBlock(0); err != nil {
		t.Fatalf("DeleteTodayBlock(0) failed: %v", err)
	}

	summary, err = GetStats()
	if err != nil {
		t.Fatalf("GetStats after deletion failed: %v", err)
	}
	if summary.TodayCount != 1 {
		t.Fatalf("Expected TodayCount 1 after deletion, got %d", summary.TodayCount)
	}
	if summary.TodayMinutes != 30 {
		t.Fatalf("Expected TodayMinutes 30 after deletion, got %d", summary.TodayMinutes)
	}
	if len(summary.TodayBlocks) != 1 {
		t.Fatalf("Expected 1 TodayBlock, got %d", len(summary.TodayBlocks))
	}
	if summary.TodayBlocks[0].Index != 0 || summary.TodayBlocks[0].Duration != 30 {
		t.Errorf("Remaining block mismatch: %+v", summary.TodayBlocks[0])
	}

	// 5. Delete remaining block index 0
	if err := DeleteTodayBlock(0); err != nil {
		t.Fatalf("DeleteTodayBlock(0) failed: %v", err)
	}

	summary, err = GetStats()
	if err != nil {
		t.Fatalf("GetStats after second deletion failed: %v", err)
	}
	if summary.TodayCount != 0 || len(summary.TodayBlocks) != 0 {
		t.Fatalf("Expected 0 today blocks after deleting all, got %d", summary.TodayCount)
	}
}
