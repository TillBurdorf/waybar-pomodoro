package ui

/*
#cgo pkg-config: gtk4
#cgo LDFLAGS: -ldl
#include <stdlib.h>
#include "gtk_ui.h"
*/
import "C"

import (
	"fmt"
	"os"
	"os/exec"
	"os/signal"
	"path/filepath"
	"runtime"
	"strconv"
	"strings"
	"sync"
	"syscall"
	"time"
	"unsafe"

	"waybar-pomodoro/internal/config"
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
			{Index: 0, StartTime: "10:00", EndTime: "10:30", Duration: 30},
			{Index: 1, StartTime: "17:00", EndTime: "17:30", Duration: 30},
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
	case "refresh_state":
		// In-place UI reload: re-apply current output & summary to new card widgets
	}

	if strings.HasPrefix(action, "delete_block ") {
		var idx int
		if _, err := fmt.Sscanf(action, "delete_block %d", &idx); err == nil && idx >= 0 {
			if idx < len(s.summary.TodayBlocks) {
				block := s.summary.TodayBlocks[idx]
				s.summary.TodayBlocks = append(s.summary.TodayBlocks[:idx], s.summary.TodayBlocks[idx+1:]...)
				for i := range s.summary.TodayBlocks {
					s.summary.TodayBlocks[i].Index = i
				}
				s.summary.TodayCount = len(s.summary.TodayBlocks)
				s.summary.TodayMinutes -= block.Duration
				if s.summary.TodayMinutes < 0 {
					s.summary.TodayMinutes = 0
				}
			}
		}
	}

	updateGTK(s.output, s.summary)
}

func getDevPidPath() string {
	return fmt.Sprintf("/tmp/waybar-pomodoro-%d-dev.pid", os.Getuid())
}

func ensureSingleDevInstance() (func(), error) {
	currentPid := os.Getpid()

	// 1. Check PID file first
	pidPath := getDevPidPath()
	if data, err := os.ReadFile(pidPath); err == nil {
		var pid int
		if _, err := fmt.Sscanf(strings.TrimSpace(string(data)), "%d", &pid); err == nil && pid > 0 && pid != currentPid {
			if proc, err := os.FindProcess(pid); err == nil {
				if err := proc.Signal(syscall.Signal(0)); err == nil {
					_ = proc.Signal(syscall.SIGTERM)
				}
			}
		}
		_ = os.Remove(pidPath)
	}

	// 2. Also scan /proc for any other running dev preview processes
	if entries, err := os.ReadDir("/proc"); err == nil {
		for _, entry := range entries {
			if !entry.IsDir() {
				continue
			}
			pid, err := strconv.Atoi(entry.Name())
			if err != nil || pid == currentPid {
				continue
			}
			cmdlineBytes, err := os.ReadFile(filepath.Join("/proc", entry.Name(), "cmdline"))
			if err != nil {
				continue
			}
			cmdline := string(cmdlineBytes)
			if strings.Contains(cmdline, "waybar-pomodoro") && strings.Contains(cmdline, "dev") {
				if proc, err := os.FindProcess(pid); err == nil {
					if err := proc.Signal(syscall.Signal(0)); err == nil {
						_ = proc.Signal(syscall.SIGTERM)
					}
				}
			}
		}
	}

	// Wait briefly for previous instances to terminate
	time.Sleep(50 * time.Millisecond)

	// Write current PID
	if err := os.WriteFile(pidPath, []byte(strconv.Itoa(currentPid)), 0o600); err != nil {
		return func() {}, err
	}

	cleanup := func() {
		_ = os.Remove(pidPath)
	}
	return cleanup, nil
}

// RunDevUI launches an isolated mock UI dev preview with interactive test keys.
func RunDevUI() error {
	cleanup, err := ensureSingleDevInstance()
	if err != nil {
		return fmt.Errorf("failed dev single-instance check: %w", err)
	}
	defer cleanup()

	runtime.LockOSThread()
	defer runtime.UnlockOSThread()

	done := make(chan struct{})
	var once sync.Once
	stop := func() { once.Do(func() { close(done) }) }
	defer stop()

	signals := make(chan os.Signal, 1)
	signal.Notify(signals, os.Interrupt, syscall.SIGTERM)
	defer signal.Stop(signals)
	go func() {
		select {
		case <-signals:
			C.pom_gtk_quit_async()
		case <-done:
		}
	}()

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
							currentDevState.summary.TodayCount++
							currentDevState.summary.TodayMinutes += 25
							cfg := config.Load()
							tc := cfg.TotalCycles
							if tc <= 0 {
								tc = 4
							}
							if currentDevState.summary.TodayCount%tc == 0 {
								currentDevState.output.Mode = "long_break"
								currentDevState.output.Total = cfg.LongBreakDurationMinutes * 60
								currentDevState.output.Remaining = cfg.LongBreakDurationMinutes * 60
							} else {
								currentDevState.output.Mode = "break"
								currentDevState.output.Total = cfg.BreakDurationMinutes * 60
								currentDevState.output.Remaining = cfg.BreakDurationMinutes * 60
							}
							currentDevState.output.Running = true
						} else {
							cfg := config.Load()
							currentDevState.output.Mode = "work"
							currentDevState.output.Total = cfg.WorkDurationMinutes * 60
							currentDevState.output.Remaining = cfg.WorkDurationMinutes * 60
							currentDevState.output.Running = true
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

	// Live hot-reload watcher for UI edits (in-place swap without destroying the Wayland window)
	go watchAndHotReloadUI(done)

	status := C.pom_gtk_dev_run()
	stop()
	if status != 0 {
		errMsg := fmt.Sprintf("GTK dev application exited with status %d", int(status))
		cMsg := C.CString(errMsg)
		defer C.free(unsafe.Pointer(cMsg))
		C.pom_gtk_dev_show_fallback_error(cMsg)
		return fmt.Errorf("%s", errMsg)
	}
	return nil
}

func findModuleRoot() string {
	dir, err := os.Getwd()
	if err != nil {
		return "."
	}
	for {
		if _, err := os.Stat(filepath.Join(dir, "go.mod")); err == nil {
			return dir
		}
		parent := filepath.Dir(dir)
		if parent == dir {
			break
		}
		dir = parent
	}
	return "."
}

func watchAndHotReloadUI(done <-chan struct{}) {
	root := findModuleRoot()
	targetPath := filepath.Join(root, "internal/ui/gtk_ui.c")
	headerPath := filepath.Join(root, "internal/ui/gtk_ui.h")

	getLatestMod := func() time.Time {
		var latest time.Time
		for _, p := range []string{targetPath, headerPath} {
			if info, err := os.Stat(p); err == nil {
				if info.ModTime().After(latest) {
					latest = info.ModTime()
				}
			}
		}
		return latest
	}

	lastMod := getLatestMod()
	tmpDir := filepath.Join(root, "tmp")
	_ = os.MkdirAll(tmpDir, 0o755)

	// Clean up older dev_ui_*.so files from previous runs
	if files, err := os.ReadDir(tmpDir); err == nil {
		for _, f := range files {
			if strings.HasPrefix(f.Name(), "dev_ui_") && strings.HasSuffix(f.Name(), ".so") {
				_ = os.Remove(filepath.Join(tmpDir, f.Name()))
			}
		}
	}

	cflagsOut, _ := exec.Command("pkg-config", "--cflags", "--libs", "gtk4").Output()
	gtk4Flags := strings.Fields(string(cflagsOut))

	ticker := time.NewTicker(150 * time.Millisecond)
	defer ticker.Stop()

	for {
		select {
		case <-done:
			return
		case <-ticker.C:
			currMod := getLatestMod()
			if currMod.After(lastMod) {
				time.Sleep(80 * time.Millisecond)
				lastMod = getLatestMod()

				soPath := filepath.Join(tmpDir, fmt.Sprintf("dev_ui_%d.so", time.Now().UnixNano()))
				args := []string{"-shared", "-fPIC", "-Wl,--unresolved-symbols=ignore-all", "-o", soPath, targetPath}
				args = append(args, gtk4Flags...)

				cmd := exec.Command("gcc", args...)
				output, compileErr := cmd.CombinedOutput()
				if compileErr != nil {
					errStr := fmt.Sprintf("UI Compile Error:\n%s", string(output))
					fmt.Fprintf(os.Stderr, "❌ %s\n", errStr)
					cErr := C.CString(errStr)
					C.pom_gtk_dev_show_compile_error(cErr)
					C.free(unsafe.Pointer(cErr))
				} else {
					fmt.Printf("⚡ UI reloaded in-place: %s\n", filepath.Base(soPath))
					cSo := C.CString(soPath)
					C.pom_gtk_dev_load_module(cSo)
					C.free(unsafe.Pointer(cSo))
				}
			}
		}
	}
}
