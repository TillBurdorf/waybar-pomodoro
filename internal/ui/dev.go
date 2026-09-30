package ui

/*
#cgo pkg-config: gtk4
#include <stdlib.h>
#include "gtk_ui.h"
*/
import "C"

import (
	"fmt"
	"runtime"
	"sync"
	"time"
	"unsafe"

	"waybar-pomodoro/internal/stats"
	"waybar-pomodoro/internal/waybar"
)

type devState struct {
	mu      sync.Mutex
	output  waybar.Output
	summary stats.StatsSummary
}

func getDevMockSummary() stats.StatsSummary {
	now := time.Now().Local()
	weekday := int(now.Weekday())
	offsetFromMonday := (weekday + 6) % 7
	monday := now.AddDate(0, 0, -offsetFromMonday)

	todayKey := now.Format("2006-01-02")
	mockMinutes := []int{120, 90, 60, 45, 100, 0, 0}
	var weekDays []stats.DayStats
	totalMin := 0

	for i := 0; i < 7; i++ {
		d := monday.AddDate(0, 0, i)
		dKey := d.Format("2006-01-02")
		isToday := (dKey == todayKey)
		mins := mockMinutes[i]
		totalMin += mins
		weekDays = append(weekDays, stats.DayStats{
			DayName: d.Format("Mon"),
			DateStr: d.Format("02 Jan"),
			Minutes: mins,
			TimeStr: stats.FormatDuration(mins),
			IsToday: isToday,
		})
	}

	return stats.StatsSummary{
		TodayCount:   2,
		TodayMinutes: 60,
		RecentHistory: []stats.SessionRecord{
			{Timestamp: now.Add(-1 * time.Hour), Mode: "work", Duration: 1800},
		},
		TodayBlocks: []stats.WorkBlock{
			{StartTime: "10:00", EndTime: "10:30", Duration: 30},
			{StartTime: "17:00", EndTime: "17:30", Duration: 30},
		},
		WeekDays:     weekDays,
		WeekTotalMin: totalMin,
	}
}

var currentDevState = &devState{
	output: waybar.Output{
		Mode:      "work",
		Remaining: 15 * 60,
		Total:     25 * 60,
		Running:   true,
	},
	summary: getDevMockSummary(),
}

//export goGTKDevAction
func goGTKDevAction(cAction *C.char) {
	action := C.GoString(cAction)
	currentDevState.handleAction(action)
}

func (s *devState) handleAction(action string) {
	s.mu.Lock()
	defer s.mu.Unlock()

	switch action {
	case "toggle":
		s.output.Running = !s.output.Running
	case "skip":
		if s.output.Mode == "work" {
			s.output.Mode = "break"
			s.output.Total = 5 * 60
			s.output.Remaining = 5 * 60
		} else {
			s.output.Mode = "work"
			s.output.Total = 25 * 60
			s.output.Remaining = 25 * 60
		}
		s.output.Running = false
	case "reset":
		if s.output.Mode == "break" {
			s.output.Remaining = 5 * 60
		} else {
			s.output.Remaining = 25 * 60
		}
		s.output.Running = false
	case "stop":
		s.output.Mode = "work"
		s.output.Total = 25 * 60
		s.output.Remaining = 25 * 60
		s.output.Running = false
	case "mode":
		if s.output.Mode == "work" {
			s.output.Mode = "break"
			s.output.Total = 5 * 60
			s.output.Remaining = 5 * 60
		} else {
			s.output.Mode = "work"
			s.output.Total = 25 * 60
			s.output.Remaining = 25 * 60
		}
	case "progress":
		remFraction := float64(s.output.Remaining) / float64(s.output.Total)
		if remFraction > 0.75 {
			s.output.Remaining = int(float64(s.output.Total) * 0.5)
		} else if remFraction > 0.4 {
			s.output.Remaining = int(float64(s.output.Total) * 0.25)
		} else if remFraction > 0.1 {
			s.output.Remaining = 5
		} else {
			s.output.Remaining = s.output.Total
		}
	case "cycle":
		s.summary.TodayCount = (s.summary.TodayCount + 1) % 8
		s.summary.TodayMinutes = s.summary.TodayCount * 25
	case "add_minute":
		if s.output.Remaining < s.output.Total {
			s.output.Remaining += 60
			if s.output.Remaining > s.output.Total {
				s.output.Remaining = s.output.Total
			}
		}
	case "sub_minute":
		if s.output.Remaining > 60 {
			s.output.Remaining -= 60
		}
	}

	updateGTK(s.output, s.summary)
}

// RunDevUI launches an isolated mock UI dev preview with interactive test keys.
func RunDevUI() error {
	runtime.LockOSThread()
	defer runtime.UnlockOSThread()

	done := make(chan struct{})
	defer close(done)

	// Initial render
	currentDevState.mu.Lock()
	initOut := currentDevState.output
	initSum := currentDevState.summary
	currentDevState.mu.Unlock()
	updateGTK(initOut, initSum)

	// Live tick loop
	go func() {
		ticker := time.NewTicker(1 * time.Second)
		defer ticker.Stop()
		for {
			select {
			case <-done:
				return
			case <-ticker.C:
				currentDevState.mu.Lock()
				if currentDevState.output.Running {
					if currentDevState.output.Remaining > 0 {
						currentDevState.output.Remaining--
					} else {
						if currentDevState.output.Mode == "work" {
							currentDevState.output.Mode = "break"
							currentDevState.output.Total = 5 * 60
							currentDevState.output.Remaining = 5 * 60
							currentDevState.summary.TodayCount++
							currentDevState.summary.TodayMinutes += 25
						} else {
							currentDevState.output.Mode = "work"
							currentDevState.output.Total = 25 * 60
							currentDevState.output.Remaining = 25 * 60
						}
					}
					out := currentDevState.output
					sum := currentDevState.summary
					updateGTK(out, sum)
				}
				currentDevState.mu.Unlock()
			}
		}
	}()

	status := C.pom_gtk_dev_run()
	if status != 0 {
		errMsg := fmt.Sprintf("GTK dev application exited with status %d", int(status))
		cMsg := C.CString(errMsg)
		defer C.free(unsafe.Pointer(cMsg))
		C.pom_gtk_dev_show_fallback_error(cMsg)
		return fmt.Errorf("%s", errMsg)
	}
	return nil
}
