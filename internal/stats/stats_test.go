package stats

import (
	"os"
	"path/filepath"
	"testing"
	"time"
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

func TestSessionActualStartAndEndTimeWithPause(t *testing.T) {
	tmpDir, err := os.MkdirTemp("", "waybar-pomodoro-stats-test")
	if err != nil {
		t.Fatalf("Failed to create temp dir: %v", err)
	}
	defer os.RemoveAll(tmpDir)

	t.Setenv("HOME", tmpDir)

	// Simulate start at 10:00, paused 20min in between, ending at 10:50 with 30m of focus
	today := time.Now().Local()
	startTime := time.Date(today.Year(), today.Month(), today.Day(), 10, 0, 0, 0, today.Location())
	endTime := time.Date(today.Year(), today.Month(), today.Day(), 10, 50, 0, 0, today.Location())
	workDurationSeconds := 30 * 60 // 30 minutes (1800s)

	if err := LogSessionWithTimes("work", workDurationSeconds, startTime, endTime); err != nil {
		t.Fatalf("LogSessionWithTimes failed: %v", err)
	}

	summary, err := GetStats()
	if err != nil {
		t.Fatalf("GetStats failed: %v", err)
	}

	if summary.TodayCount != 1 {
		t.Fatalf("Expected 1 session, got %d", summary.TodayCount)
	}
	if summary.TodayMinutes != 30 {
		t.Fatalf("Expected 30 focus minutes, got %d", summary.TodayMinutes)
	}
	if len(summary.TodayBlocks) != 1 {
		t.Fatalf("Expected 1 TodayBlock, got %d", len(summary.TodayBlocks))
	}

	block := summary.TodayBlocks[0]
	if block.StartTime != "10:00" {
		t.Errorf("Expected StartTime '10:00', got '%s'", block.StartTime)
	}
	if block.EndTime != "10:50" {
		t.Errorf("Expected EndTime '10:50', got '%s'", block.EndTime)
	}
	if block.Duration != 30 {
		t.Errorf("Expected Duration 30, got %d", block.Duration)
	}
}

func TestProjectAssignmentAndAggregation(t *testing.T) {
	tmpDir, err := os.MkdirTemp("", "waybar-pomodoro-project-test")
	if err != nil {
		t.Fatalf("Failed to create temp dir: %v", err)
	}
	defer os.RemoveAll(tmpDir)

	t.Setenv("HOME", tmpDir)

	// Log two work sessions today
	if err := LogSession("work", 1800); err != nil { // 30 mins
		t.Fatalf("LogSession 1 failed: %v", err)
	}
	if err := LogSession("work", 1200); err != nil { // 20 mins
		t.Fatalf("LogSession 2 failed: %v", err)
	}

	summary, err := GetStats()
	if err != nil {
		t.Fatalf("GetStats failed: %v", err)
	}
	if len(summary.TodayBlocks) != 2 {
		t.Fatalf("Expected 2 TodayBlocks, got %d", len(summary.TodayBlocks))
	}

	// Initially neither has a project assigned
	if summary.TodayBlocks[0].Project != "" || summary.TodayBlocks[1].Project != "" {
		t.Errorf("Expected empty initial projects, got %+v", summary.TodayBlocks)
	}

	// Assign project "Waybar" to block 0
	if err := SetTodayBlockProject(0, "Waybar"); err != nil {
		t.Fatalf("SetTodayBlockProject(0, 'Waybar') failed: %v", err)
	}

	// Assign project "Dotfiles" to block 1
	if err := SetTodayBlockProject(1, "Dotfiles"); err != nil {
		t.Fatalf("SetTodayBlockProject(1, 'Dotfiles') failed: %v", err)
	}

	summary, err = GetStats()
	if err != nil {
		t.Fatalf("GetStats failed: %v", err)
	}

	if summary.TodayBlocks[0].Project != "Waybar" {
		t.Errorf("Expected block 0 to have project 'Waybar', got '%s'", summary.TodayBlocks[0].Project)
	}
	if summary.TodayBlocks[1].Project != "Dotfiles" {
		t.Errorf("Expected block 1 to have project 'Dotfiles', got '%s'", summary.TodayBlocks[1].Project)
	}

	// Verify AllProjects list
	if len(summary.AllProjects) != 2 {
		t.Fatalf("Expected 2 AllProjects, got %d: %+v", len(summary.AllProjects), summary.AllProjects)
	}

	// Verify ProjectSummaries
	if len(summary.ProjectSummaries) != 2 {
		t.Fatalf("Expected 2 ProjectSummaries, got %d: %+v", len(summary.ProjectSummaries), summary.ProjectSummaries)
	}
	var waybarSum, dotfilesSum *ProjectSummary
	for i := range summary.ProjectSummaries {
		if summary.ProjectSummaries[i].Name == "Waybar" {
			waybarSum = &summary.ProjectSummaries[i]
		}
		if summary.ProjectSummaries[i].Name == "Dotfiles" {
			dotfilesSum = &summary.ProjectSummaries[i]
		}
	}
	if waybarSum == nil || waybarSum.Minutes != 30 || waybarSum.SessionCount != 1 {
		t.Errorf("Unexpected Waybar summary: %+v", waybarSum)
	}
	if dotfilesSum == nil || dotfilesSum.Minutes != 20 || dotfilesSum.SessionCount != 1 {
		t.Errorf("Unexpected Dotfiles summary: %+v", dotfilesSum)
	}

	// Verify PastSessions
	if len(summary.PastSessions) != 2 {
		t.Fatalf("Expected 2 PastSessions, got %d", len(summary.PastSessions))
	}

	// Clear project from block 0 (pass empty string)
	if err := SetTodayBlockProject(0, ""); err != nil {
		t.Fatalf("SetTodayBlockProject(0, '') failed: %v", err)
	}
	summary, err = GetStats()
	if err != nil {
		t.Fatalf("GetStats failed: %v", err)
	}
	if summary.TodayBlocks[0].Project != "" {
		t.Errorf("Expected empty project for block 0, got '%s'", summary.TodayBlocks[0].Project)
	}
	if len(summary.ProjectSummaries) != 1 || summary.ProjectSummaries[0].Name != "Dotfiles" {
		t.Errorf("Expected only Dotfiles summary remaining, got: %+v", summary.ProjectSummaries)
	}
}

func TestAddManualSession(t *testing.T) {
	tmpDir, err := os.MkdirTemp("", "waybar-pomodoro-manual-session-test")
	if err != nil {
		t.Fatalf("Failed to create temp dir: %v", err)
	}
	defer os.RemoveAll(tmpDir)

	t.Setenv("HOME", tmpDir)

	// 1. Add session with explicit start, end, duration, and project
	err = AddManualSession("10:00", "10:30", 30, "Frontend")
	if err != nil {
		t.Fatalf("AddManualSession failed: %v", err)
	}

	summary, err := GetStats()
	if err != nil {
		t.Fatalf("GetStats failed: %v", err)
	}

	if summary.TodayCount != 1 {
		t.Fatalf("Expected 1 session today, got %d", summary.TodayCount)
	}
	if summary.TodayMinutes != 30 {
		t.Fatalf("Expected 30 today minutes, got %d", summary.TodayMinutes)
	}
	if len(summary.TodayBlocks) != 1 {
		t.Fatalf("Expected 1 today block, got %d", len(summary.TodayBlocks))
	}
	b := summary.TodayBlocks[0]
	if b.StartTime != "10:00" || b.EndTime != "10:30" || b.Duration != 30 || b.Project != "Frontend" {
		t.Errorf("Unexpected today block content: %+v", b)
	}

	// 2. Add another session without project and auto duration
	err = AddManualSession("11:00", "11:45", 0, "")
	if err != nil {
		t.Fatalf("AddManualSession without project failed: %v", err)
	}

	summary, err = GetStats()
	if err != nil {
		t.Fatalf("GetStats failed: %v", err)
	}
	if summary.TodayCount != 2 {
		t.Fatalf("Expected 2 sessions today, got %d", summary.TodayCount)
	}
	if summary.TodayMinutes != 75 { // 30 + 45
		t.Fatalf("Expected 75 today minutes, got %d", summary.TodayMinutes)
	}
	if len(summary.TodayBlocks) != 2 {
		t.Fatalf("Expected 2 today blocks, got %d", len(summary.TodayBlocks))
	}
	b2 := summary.TodayBlocks[1]
	if b2.StartTime != "11:00" || b2.EndTime != "11:45" || b2.Duration != 45 || b2.Project != "" {
		t.Errorf("Unexpected second today block content: %+v", b2)
	}

	// 3. Edit block index 0 (change start to 09:30, end to 10:30, duration to 60, project to Backend)
	err = UpdateTodayBlock(0, "09:30", "10:30", 60, "Backend")
	if err != nil {
		t.Fatalf("UpdateTodayBlock failed: %v", err)
	}

	summary, err = GetStats()
	if err != nil {
		t.Fatalf("GetStats failed: %v", err)
	}
	if summary.TodayCount != 2 {
		t.Fatalf("Expected 2 sessions today, got %d", summary.TodayCount)
	}
	if summary.TodayMinutes != 105 { // 60 + 45
		t.Fatalf("Expected 105 today minutes after edit, got %d", summary.TodayMinutes)
	}
	bEdited := summary.TodayBlocks[0]
	if bEdited.StartTime != "09:30" || bEdited.EndTime != "10:30" || bEdited.Duration != 60 || bEdited.Project != "Backend" {
		t.Errorf("Unexpected edited block content: %+v", bEdited)
	}
}


