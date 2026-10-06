package timer

import (
	"os"
	"testing"
	"time"

	"waybar-pomodoro/internal/stats"
)

func TestSkipWorkSessionRecordsActualTimeAndTransitionsToBreak(t *testing.T) {
	tmpDir, err := os.MkdirTemp("", "waybar-pomodoro-timer-test")
	if err != nil {
		t.Fatalf("Failed to create temp dir: %v", err)
	}
	defer os.RemoveAll(tmpDir)

	t.Setenv("HOME", tmpDir)

	tm := New()
	// Set 30m work duration (1800s)
	tm.workDuration = 1800
	tm.breakDuration = 300
	tm.longBreakDuration = 900
	tm.totalCycles = 4

	// Simulate that the session was running and is now at 15m remaining (15m elapsed)
	now := time.Now()
	startTime := now.Add(-15 * time.Minute)
	tm.SetStateForTest("work", 900, 0, true, startTime)

	// User ends the session early via Skip
	tm.Skip()

	// Verify timer state
	if tm.Mode() != "break" {
		t.Errorf("Expected mode 'break', got '%s'", tm.Mode())
	}
	if tm.Remaining() != 300 {
		t.Errorf("Expected remaining 300, got %d", tm.Remaining())
	}
	if tm.Running() {
		t.Errorf("Expected running to be false after skip")
	}
	if tm.CompletedCycles() != 1 {
		t.Errorf("Expected completedCycles 1, got %d", tm.CompletedCycles())
	}

	// Verify history record in stats
	summary, err := stats.GetStats()
	if err != nil {
		t.Fatalf("GetStats failed: %v", err)
	}

	if summary.TodayCount != 1 {
		t.Fatalf("Expected TodayCount 1, got %d", summary.TodayCount)
	}
	if summary.TodayMinutes != 15 {
		t.Fatalf("Expected TodayMinutes 15, got %d", summary.TodayMinutes)
	}
	if len(summary.TodayBlocks) != 1 {
		t.Fatalf("Expected 1 TodayBlock, got %d", len(summary.TodayBlocks))
	}
	if summary.TodayBlocks[0].Duration != 15 {
		t.Errorf("Expected TodayBlock duration 15, got %d", summary.TodayBlocks[0].Duration)
	}
	if summary.TodayBlocks[0].StartTime != startTime.Local().Format("15:04") {
		t.Errorf("Expected block start time %s, got %s", startTime.Local().Format("15:04"), summary.TodayBlocks[0].StartTime)
	}
}

func TestSkipWorkSessionTransitionsToLongBreakWhenCyclesReached(t *testing.T) {
	tmpDir, err := os.MkdirTemp("", "waybar-pomodoro-timer-test")
	if err != nil {
		t.Fatalf("Failed to create temp dir: %v", err)
	}
	defer os.RemoveAll(tmpDir)

	t.Setenv("HOME", tmpDir)

	tm := New()
	tm.workDuration = 1500
	tm.breakDuration = 300
	tm.longBreakDuration = 900
	tm.totalCycles = 4

	// 3 cycles already completed, this 4th session ran for 10m (900s remaining out of 1500s)
	tm.SetStateForTest("work", 900, 3, true, time.Now().Add(-10*time.Minute))

	tm.Skip()

	if tm.Mode() != "long_break" {
		t.Errorf("Expected mode 'long_break', got '%s'", tm.Mode())
	}
	if tm.Remaining() != 900 {
		t.Errorf("Expected remaining 900, got %d", tm.Remaining())
	}
	if tm.CompletedCycles() != 4 {
		t.Errorf("Expected completedCycles 4, got %d", tm.CompletedCycles())
	}

	summary, err := stats.GetStats()
	if err != nil {
		t.Fatalf("GetStats failed: %v", err)
	}
	if summary.TodayCount != 1 || summary.TodayMinutes != 10 {
		t.Errorf("Unexpected stats: count=%d, minutes=%d", summary.TodayCount, summary.TodayMinutes)
	}
}

func TestSkipZeroElapsedDoesNotRecordStats(t *testing.T) {
	tmpDir, err := os.MkdirTemp("", "waybar-pomodoro-timer-test")
	if err != nil {
		t.Fatalf("Failed to create temp dir: %v", err)
	}
	defer os.RemoveAll(tmpDir)

	t.Setenv("HOME", tmpDir)

	tm := New()
	tm.workDuration = 1500
	tm.SetStateForTest("work", 1500, 0, false, time.Time{})

	tm.Skip()

	if tm.Mode() != "break" {
		t.Errorf("Expected mode 'break', got '%s'", tm.Mode())
	}
	if tm.CompletedCycles() != 0 {
		t.Errorf("Expected completedCycles to remain 0, got %d", tm.CompletedCycles())
	}

	summary, err := stats.GetStats()
	if err != nil {
		t.Fatalf("GetStats failed: %v", err)
	}
	if summary.TodayCount != 0 {
		t.Errorf("Expected 0 sessions recorded, got %d", summary.TodayCount)
	}
}

func TestSkipBreakAndLongBreak(t *testing.T) {
	tm := New()
	tm.workDuration = 1500
	tm.breakDuration = 300
	tm.longBreakDuration = 900

	// From break to work
	tm.SetStateForTest("break", 200, 2, true, time.Time{})
	tm.Skip()
	if tm.Mode() != "work" || tm.Remaining() != 1500 {
		t.Errorf("Expected mode work with 1500s remaining, got %s, %d", tm.Mode(), tm.Remaining())
	}
	if tm.CompletedCycles() != 2 {
		t.Errorf("Expected completedCycles 2, got %d", tm.CompletedCycles())
	}

	// From long_break to work resets completedCycles
	tm.SetStateForTest("long_break", 400, 4, true, time.Time{})
	tm.Skip()
	if tm.Mode() != "work" || tm.Remaining() != 1500 {
		t.Errorf("Expected mode work with 1500s remaining, got %s, %d", tm.Mode(), tm.Remaining())
	}
	if tm.CompletedCycles() != 0 {
		t.Errorf("Expected completedCycles reset to 0, got %d", tm.CompletedCycles())
	}
}

func TestPausePreservesSessionStartTime(t *testing.T) {
	tmpDir, err := os.MkdirTemp("", "waybar-pomodoro-timer-test")
	if err != nil {
		t.Fatalf("Failed to create temp dir: %v", err)
	}
	defer os.RemoveAll(tmpDir)

	t.Setenv("HOME", tmpDir)

	tm := New()
	tm.workDuration = 1800 // 30m

	// Start running
	tm.Toggle()
	if !tm.Running() {
		t.Fatalf("Expected timer to be running")
	}
	origStart := tm.SessionStartTime()
	if origStart.IsZero() {
		t.Fatalf("Expected sessionStartTime to be set")
	}

	// Pause
	tm.Toggle()
	if tm.Running() {
		t.Fatalf("Expected timer to be paused")
	}
	if tm.SessionStartTime() != origStart {
		t.Errorf("Pause changed sessionStartTime: got %v, expected %v", tm.SessionStartTime(), origStart)
	}

	// Resume
	tm.Toggle()
	if !tm.Running() {
		t.Fatalf("Expected timer to be running again")
	}
	if tm.SessionStartTime() != origStart {
		t.Errorf("Resume changed sessionStartTime: got %v, expected %v", tm.SessionStartTime(), origStart)
	}
}
