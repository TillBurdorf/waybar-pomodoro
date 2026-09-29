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

	"waybar-pomodoro/internal/stats"
	"waybar-pomodoro/internal/waybar"
)

type devState struct {
	mu      sync.Mutex
	output  waybar.Output
	summary stats.StatsSummary
}

var currentDevState = &devState{
	output: waybar.Output{
		Mode:      "work",
		Remaining: 15 * 60,
		Total:     25 * 60,
		Running:   true,
	},
	summary: stats.StatsSummary{
		TodayCount:   3,
		TodayMinutes: 75,
		RecentHistory: []stats.SessionRecord{
			{Timestamp: time.Now().Add(-20 * time.Minute), Mode: "work", Duration: 1500},
		},
	},
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
		return fmt.Errorf("GTK dev application exited with status %d", int(status))
	}
	return nil
}
