package stats

import (
	"bufio"
	"encoding/json"
	"fmt"
	"os"
	"path/filepath"
	"sort"
	"strings"
	"time"
)

type SessionRecord struct {
	Timestamp time.Time  `json:"timestamp"`
	StartTime *time.Time `json:"start_time,omitempty"`
	Mode      string     `json:"mode"`
	Duration  int        `json:"duration_seconds"`
	Project   string     `json:"project,omitempty"`
}

type WorkBlock struct {
	Index     int    `json:"index"`
	StartTime string // "10:00"
	EndTime   string // "10:30"
	Duration  int    // minutes
	Project   string `json:"project,omitempty"`
}

type DayStats struct {
	DayName string // "Mon"
	DateStr string // "28 Sep"
	Minutes int    // total minutes
	TimeStr string // "2h 30m"
	IsToday bool
}

type ProjectSummary struct {
	Name         string `json:"name"`
	Minutes      int    `json:"minutes"`
	TimeStr      string `json:"time_str"`
	SessionCount int    `json:"session_count"`
}

type PastSession struct {
	DateStr   string `json:"date_str"`
	StartTime string `json:"start_time"`
	EndTime   string `json:"end_time"`
	Duration  int    `json:"duration"`
	Project   string `json:"project,omitempty"`
}

type StatsSummary struct {
	TodayCount       int
	TodayMinutes     int
	TotalCount       int
	TotalMinutes     int
	RecentHistory    []SessionRecord
	TodayBlocks      []WorkBlock
	WeekDays         []DayStats
	WeekTotalMin     int
	AllProjects      []string
	ProjectSummaries []ProjectSummary
	PastSessions     []PastSession
}

func FormatDuration(minutes int) string {
	if minutes <= 0 {
		return "0m"
	}
	h := minutes / 60
	m := minutes % 60
	if h > 0 && m > 0 {
		return fmt.Sprintf("%dh %02dm", h, m)
	} else if h > 0 {
		return fmt.Sprintf("%dh", h)
	}
	return fmt.Sprintf("%dm", m)
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
	now := time.Now()
	startTime := now.Add(-time.Duration(durationSeconds) * time.Second)
	return LogSessionWithTimes(mode, durationSeconds, startTime, now)
}

func LogSessionWithTimes(mode string, durationSeconds int, startTime, endTime time.Time) error {
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
		Timestamp: endTime,
		StartTime: &startTime,
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
				mins := rec.Duration / 60
				if mins <= 0 && rec.Duration > 0 {
					mins = 1
				}
				summary.TotalCount++
				summary.TotalMinutes += mins
				if rec.Timestamp.Local().Format("2006-01-02") == today {
					summary.TodayCount++
					summary.TodayMinutes += mins
				}
			}
			allRecords = append(allRecords, rec)
		}
	}

	// Last 5 sessions (newest first)
	for i := len(allRecords) - 1; i >= 0 && len(summary.RecentHistory) < 5; i-- {
		summary.RecentHistory = append(summary.RecentHistory, allRecords[i])
	}

	// Today's workblocks in chronological order
	blockIdx := 0
	for _, rec := range allRecords {
		if rec.Mode == "work" && rec.Timestamp.Local().Format("2006-01-02") == today {
			mins := rec.Duration / 60
			if mins <= 0 && rec.Duration > 0 {
				mins = 1
			}
			endTime := rec.Timestamp.Local()
			startTime := endTime.Add(-time.Duration(rec.Duration) * time.Second)
			if rec.StartTime != nil && !rec.StartTime.IsZero() {
				startTime = rec.StartTime.Local()
			}
			summary.TodayBlocks = append(summary.TodayBlocks, WorkBlock{
				Index:     blockIdx,
				StartTime: startTime.Format("15:04"),
				EndTime:   endTime.Format("15:04"),
				Duration:  mins,
				Project:   strings.TrimSpace(rec.Project),
			})
			blockIdx++
		}
	}

	// Weekly statistics (Monday through Sunday of current week)
	now := time.Now().Local()
	weekday := int(now.Weekday())
	offsetFromMonday := (weekday + 6) % 7
	monday := now.AddDate(0, 0, -offsetFromMonday)

	dayMinutes := make(map[string]int)
	for _, rec := range allRecords {
		if rec.Mode == "work" {
			dayKey := rec.Timestamp.Local().Format("2006-01-02")
			mins := rec.Duration / 60
			if mins <= 0 && rec.Duration > 0 {
				mins = 1
			}
			dayMinutes[dayKey] += mins
		}
	}

	for i := 0; i < 7; i++ {
		d := monday.AddDate(0, 0, i)
		dKey := d.Format("2006-01-02")
		mins := dayMinutes[dKey]
		summary.WeekTotalMin += mins
		summary.WeekDays = append(summary.WeekDays, DayStats{
			DayName: d.Format("Mon"),
			DateStr: d.Format("02 Jan"),
			Minutes: mins,
			TimeStr: FormatDuration(mins),
			IsToday: dKey == today,
		})
	}

	// Project aggregations and past sessions (newest first)
	projectMinutes := make(map[string]int)
	projectCounts := make(map[string]int)
	uniqueProjects := make(map[string]struct{})

	for i := len(allRecords) - 1; i >= 0; i-- {
		rec := allRecords[i]
		if rec.Mode != "work" {
			continue
		}
		mins := rec.Duration / 60
		if mins <= 0 && rec.Duration > 0 {
			mins = 1
		}
		endTime := rec.Timestamp.Local()
		startTime := endTime.Add(-time.Duration(rec.Duration) * time.Second)
		if rec.StartTime != nil && !rec.StartTime.IsZero() {
			startTime = rec.StartTime.Local()
		}

		proj := strings.TrimSpace(rec.Project)
		if proj != "" {
			uniqueProjects[proj] = struct{}{}
			projectMinutes[proj] += mins
			projectCounts[proj]++
		}

		summary.PastSessions = append(summary.PastSessions, PastSession{
			DateStr:   endTime.Format("02 Jan"),
			StartTime: startTime.Format("15:04"),
			EndTime:   endTime.Format("15:04"),
			Duration:  mins,
			Project:   proj,
		})
	}

	for p := range uniqueProjects {
		summary.AllProjects = append(summary.AllProjects, p)
	}
	sort.Strings(summary.AllProjects)

	for _, p := range summary.AllProjects {
		mins := projectMinutes[p]
		summary.ProjectSummaries = append(summary.ProjectSummaries, ProjectSummary{
			Name:         p,
			Minutes:      mins,
			TimeStr:      FormatDuration(mins),
			SessionCount: projectCounts[p],
		})
	}

	return summary, nil
}

func SetTodayBlockProject(index int, project string) error {
	filePath, err := getStatsFilePath()
	if err != nil {
		return err
	}

	file, err := os.Open(filePath)
	if os.IsNotExist(err) {
		return nil
	} else if err != nil {
		return err
	}

	scanner := bufio.NewScanner(file)
	today := time.Now().Format("2006-01-02")
	var records []SessionRecord
	todayWorkCount := 0

	for scanner.Scan() {
		var rec SessionRecord
		if err := json.Unmarshal(scanner.Bytes(), &rec); err == nil {
			if rec.Mode == "work" && rec.Timestamp.Local().Format("2006-01-02") == today {
				if todayWorkCount == index {
					rec.Project = strings.TrimSpace(project)
				}
				todayWorkCount++
			}
			records = append(records, rec)
		}
	}
	file.Close()

	tmpPath := filePath + ".tmp"
	tmpFile, err := os.Create(tmpPath)
	if err != nil {
		return err
	}

	for _, rec := range records {
		bytes, err := json.Marshal(rec)
		if err != nil {
			continue
		}
		if _, err := tmpFile.Write(append(bytes, '\n')); err != nil {
			tmpFile.Close()
			return err
		}
	}
	tmpFile.Close()

	return os.Rename(tmpPath, filePath)
}

func UpdateTodayBlock(index int, startTimeStr, endTimeStr string, durationMinutes int, project string) error {
	filePath, err := getStatsFilePath()
	if err != nil {
		return err
	}

	file, err := os.Open(filePath)
	if os.IsNotExist(err) {
		return nil
	} else if err != nil {
		return err
	}

	scanner := bufio.NewScanner(file)
	now := time.Now().Local()
	today := now.Format("2006-01-02")
	var records []SessionRecord
	todayWorkCount := 0

	for scanner.Scan() {
		var rec SessionRecord
		if err := json.Unmarshal(scanner.Bytes(), &rec); err == nil {
			if rec.Mode == "work" && rec.Timestamp.Local().Format("2006-01-02") == today {
				if todayWorkCount == index {
					var start, end time.Time
					st := strings.TrimSpace(startTimeStr)
					et := strings.TrimSpace(endTimeStr)

					if st != "" {
						if t, err := time.ParseInLocation("2006-01-02 15:04", today+" "+st, time.Local); err == nil {
							start = t
						}
					}
					if et != "" {
						if t, err := time.ParseInLocation("2006-01-02 15:04", today+" "+et, time.Local); err == nil {
							end = t
						}
					}

					if !start.IsZero() && !end.IsZero() {
						calcMins := int(end.Sub(start).Minutes())
						if calcMins < 0 {
							calcMins += 24 * 60
						}
						if durationMinutes <= 0 {
							durationMinutes = calcMins
						}
					} else if !start.IsZero() && end.IsZero() {
						if durationMinutes <= 0 {
							durationMinutes = rec.Duration / 60
						}
						end = start.Add(time.Duration(durationMinutes) * time.Minute)
					} else if start.IsZero() && !end.IsZero() {
						if durationMinutes <= 0 {
							durationMinutes = rec.Duration / 60
						}
						start = end.Add(-time.Duration(durationMinutes) * time.Minute)
					} else {
						if rec.StartTime != nil && !rec.StartTime.IsZero() {
							start = *rec.StartTime
						} else {
							start = rec.Timestamp.Add(-time.Duration(rec.Duration) * time.Second)
						}
						if durationMinutes > 0 {
							end = start.Add(time.Duration(durationMinutes) * time.Minute)
						} else {
							end = rec.Timestamp
							durationMinutes = rec.Duration / 60
						}
					}

					if durationMinutes <= 0 {
						durationMinutes = 1
					}

					rec.StartTime = &start
					rec.Timestamp = end
					rec.Duration = durationMinutes * 60
					rec.Project = strings.TrimSpace(project)
				}
				todayWorkCount++
			}
			records = append(records, rec)
		}
	}
	file.Close()

	tmpPath := filePath + ".tmp"
	tmpFile, err := os.Create(tmpPath)
	if err != nil {
		return err
	}

	for _, rec := range records {
		bytes, err := json.Marshal(rec)
		if err != nil {
			continue
		}
		if _, err := tmpFile.Write(append(bytes, '\n')); err != nil {
			tmpFile.Close()
			return err
		}
	}
	tmpFile.Close()

	return os.Rename(tmpPath, filePath)
}

func DeleteTodayBlock(index int) error {
	filePath, err := getStatsFilePath()
	if err != nil {
		return err
	}

	file, err := os.Open(filePath)
	if os.IsNotExist(err) {
		return nil
	} else if err != nil {
		return err
	}

	scanner := bufio.NewScanner(file)
	today := time.Now().Format("2006-01-02")
	var records []SessionRecord
	todayWorkCount := 0

	for scanner.Scan() {
		var rec SessionRecord
		if err := json.Unmarshal(scanner.Bytes(), &rec); err == nil {
			if rec.Mode == "work" && rec.Timestamp.Local().Format("2006-01-02") == today {
				if todayWorkCount == index {
					todayWorkCount++
					continue
				}
				todayWorkCount++
			}
			records = append(records, rec)
		}
	}
	file.Close()

	tmpPath := filePath + ".tmp"
	tmpFile, err := os.Create(tmpPath)
	if err != nil {
		return err
	}

	for _, rec := range records {
		bytes, err := json.Marshal(rec)
		if err != nil {
			continue
		}
		if _, err := tmpFile.Write(append(bytes, '\n')); err != nil {
			tmpFile.Close()
			return err
		}
	}
	tmpFile.Close()

	return os.Rename(tmpPath, filePath)
}

func AddManualSession(startTimeStr, endTimeStr string, durationMinutes int, project string) error {
	now := time.Now().Local()
	today := now.Format("2006-01-02")

	var start, end time.Time

	startTimeStr = strings.TrimSpace(startTimeStr)
	endTimeStr = strings.TrimSpace(endTimeStr)

	if startTimeStr != "" {
		if t, err := time.ParseInLocation("2006-01-02 15:04", today+" "+startTimeStr, time.Local); err == nil {
			start = t
		}
	}

	if endTimeStr != "" {
		if t, err := time.ParseInLocation("2006-01-02 15:04", today+" "+endTimeStr, time.Local); err == nil {
			end = t
		}
	}

	if !start.IsZero() && !end.IsZero() {
		calcMins := int(end.Sub(start).Minutes())
		if calcMins < 0 {
			calcMins += 24 * 60
		}
		if durationMinutes <= 0 {
			durationMinutes = calcMins
		}
	} else if !start.IsZero() && end.IsZero() {
		if durationMinutes <= 0 {
			durationMinutes = 25
		}
		end = start.Add(time.Duration(durationMinutes) * time.Minute)
	} else if start.IsZero() && !end.IsZero() {
		if durationMinutes <= 0 {
			durationMinutes = 25
		}
		start = end.Add(-time.Duration(durationMinutes) * time.Minute)
	} else {
		if durationMinutes <= 0 {
			durationMinutes = 25
		}
		end = now
		start = end.Add(-time.Duration(durationMinutes) * time.Minute)
	}

	if durationMinutes <= 0 {
		durationMinutes = 1
	}

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
		Timestamp: end,
		StartTime: &start,
		Mode:      "work",
		Duration:  durationMinutes * 60,
		Project:   strings.TrimSpace(project),
	}

	bytes, err := json.Marshal(rec)
	if err != nil {
		return err
	}
	_, err = file.Write(append(bytes, '\n'))
	return err
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
