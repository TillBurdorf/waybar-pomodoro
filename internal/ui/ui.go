package ui

/*
#cgo pkg-config: gtk4
#include <stdlib.h>
#include "gtk_ui.h"
*/
import "C"

import (
	"bufio"
	"encoding/json"
	"fmt"
	"net"
	"os"
	"os/signal"
	"runtime"
	"strconv"
	"strings"
	"sync"
	"syscall"
	"time"
	"unsafe"

	"waybar-pomodoro/internal/config"
	"waybar-pomodoro/internal/ipc"
	"waybar-pomodoro/internal/stats"
	"waybar-pomodoro/internal/waybar"
)

//export goGTKCommand
func goGTKCommand(command *C.char) {
	cmd := C.GoString(command)
	go func() { _ = ipc.SendCommand(cmd) }()
}

//export goGTKSetDurations
func goGTKSetDurations(workMin, breakMin C.int) {
	w := int(workMin)
	b := int(breakMin)
	cfg := config.Load()
	cfg.WorkDurationMinutes = w
	cfg.BreakDurationMinutes = b
	_ = config.Save(cfg)
	go func() {
		_ = ipc.SendCommand(fmt.Sprintf("set_settings %d %d %d %d", w, b, cfg.LongBreakDurationMinutes, cfg.TotalCycles))
	}()
}

//export goGTKSetSettings
func goGTKSetSettings(workMin, breakMin, longBreakMin, totalCycles C.int) {
	w := int(workMin)
	b := int(breakMin)
	lb := int(longBreakMin)
	tc := int(totalCycles)
	cfg := config.Config{
		WorkDurationMinutes:      w,
		BreakDurationMinutes:     b,
		LongBreakDurationMinutes: lb,
		TotalCycles:              tc,
	}
	_ = config.Save(cfg)
	go func() {
		_ = ipc.SendCommand(fmt.Sprintf("set_settings %d %d %d %d", w, b, lb, tc))
	}()
}

func getUIPidPath() string {
	return fmt.Sprintf("/tmp/waybar-pomodoro-%d-ui.pid", os.Getuid())
}

func toggleOrAcquireSingleInstanceAt(pidPath string) (bool, error) {
	if data, err := os.ReadFile(pidPath); err == nil {
		var pid int
		if _, err := fmt.Sscanf(strings.TrimSpace(string(data)), "%d", &pid); err == nil && pid > 0 {
			proc, err := os.FindProcess(pid)
			if err == nil {
				if err := proc.Signal(syscall.Signal(0)); err == nil {
					_ = proc.Signal(syscall.SIGTERM)
					return true, nil
				}
			}
		}
		_ = os.Remove(pidPath)
	}

	if err := os.WriteFile(pidPath, []byte(strconv.Itoa(os.Getpid())), 0o600); err != nil {
		return false, err
	}
	return false, nil
}

// RunUI starts the GTK4 implementation of the popup.
func RunUI() error {
	pidPath := getUIPidPath()
	toggledOff, err := toggleOrAcquireSingleInstanceAt(pidPath)
	if err != nil {
		return fmt.Errorf("failed single-instance check: %w", err)
	}
	if toggledOff {
		return nil
	}
	defer os.Remove(pidPath)
	if err := ipc.EnsureDaemonRunning(); err != nil {
		return fmt.Errorf("failed to start daemon: %w", err)
	}

	runtime.LockOSThread()
	defer runtime.UnlockOSThread()

	done := make(chan struct{})
	var once sync.Once
	stop := func() { once.Do(func() { close(done); _ = os.Remove(pidPath) }) }
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

	updates := make(chan waybar.Output, 16)
	go subscribeToDaemon(updates, done)

	summary, _ := stats.GetStats()
	updateGTK(waybar.Output{Mode: "work", Remaining: 25 * 60, Total: 25 * 60}, summary)

	go func() {
		for {
			select {
			case <-done:
				return
			case state := <-updates:
				if current, err := stats.GetStats(); err == nil {
					summary = current
				}
				updateGTK(state, summary)
			}
		}
	}()

	cfg := config.Load()
	C.pom_gtk_set_initial_settings(C.int(cfg.WorkDurationMinutes), C.int(cfg.BreakDurationMinutes), C.int(cfg.LongBreakDurationMinutes), C.int(cfg.TotalCycles))

	status := C.pom_gtk_run()
	stop()
	if status != 0 {
		return fmt.Errorf("GTK application exited with status %d", int(status))
	}
	return nil
}

// RunGTKUI is an alias to RunUI for backwards compatibility.
func RunGTKUI() error {
	return RunUI()
}

// RunToast launches a small floating GTK4 toast alert window.
func RunToast(title, message string) error {
	runtime.LockOSThread()
	defer runtime.UnlockOSThread()

	cTitle := C.CString(title)
	defer C.free(unsafe.Pointer(cTitle))

	cMsg := C.CString(message)
	defer C.free(unsafe.Pointer(cMsg))

	status := C.pom_gtk_run_toast(cTitle, cMsg)
	if status != 0 {
		return fmt.Errorf("GTK toast exited with status %d", int(status))
	}
	return nil
}

func updateGTK(state waybar.Output, summary stats.StatsSummary) {
	modeName := "FOCUS"
	if state.Mode == "break" {
		modeName = "SHORT BREAK"
	} else if state.Mode == "long_break" {
		modeName = "LONG BREAK"
	}
	status := "Paused"
	if state.Running {
		status = "Running"
	}
	total := state.Total
	if total <= 0 {
		total = 25 * 60
		if state.Mode == "break" {
			total = 5 * 60
		} else if state.Mode == "long_break" {
			total = 15 * 60
		}
	}
	elapsed := total - state.Remaining
	if elapsed < 0 {
		elapsed = 0
	} else if elapsed > total {
		elapsed = total
	}
	fraction := float64(elapsed) / float64(total)

	cfg := config.Load()
	totCycles := cfg.TotalCycles
	if totCycles <= 0 {
		totCycles = 4
	}
	completedInSet := summary.TodayCount % totCycles
	if state.Mode == "long_break" {
		completedInSet = totCycles
	}
	dots := strings.Repeat("●  ", completedInSet) + strings.Repeat("○  ", totCycles-completedInSet)
	cycleText := strings.TrimSpace(dots)
	history := "No sessions yet today"
	if len(summary.RecentHistory) > 0 {
		record := summary.RecentHistory[0]
		phase := "Focus"
		if record.Mode == "break" {
			phase = "Break"
		}
		mins := record.Duration / 60
		if mins <= 0 && record.Duration > 0 {
			mins = 1
		}
		history = fmt.Sprintf("Last: %s · %s (%d min)", record.Timestamp.Local().Format("15:04"), phase, mins)
	}
	values := []string{
		modeName,
		fmt.Sprintf("%02d:%02d", state.Remaining/60, state.Remaining%60),
		status,
		"",
		cycleText,
		fmt.Sprintf("%d sessions · %d min focus", summary.TodayCount, summary.TodayMinutes),
		history,
	}
	cs := make([]*C.char, len(values))
	for i, value := range values {
		cs[i] = C.CString(value)
		defer C.free(unsafe.Pointer(cs[i]))
	}
	C.pom_gtk_update(cs[0], cs[1], cs[2], cs[3], cs[4], cs[5], cs[6], C.double(fraction))

	todaySumText := fmt.Sprintf("%d sessions · %s focus", summary.TodayCount, stats.FormatDuration(summary.TodayMinutes))
	todaySummaryC := C.CString(todaySumText)
	defer C.free(unsafe.Pointer(todaySummaryC))

	var blockLines []string
	for _, b := range summary.TodayBlocks {
		durStr := stats.FormatDuration(b.Duration)
		blockLines = append(blockLines, fmt.Sprintf("%d|%s|%s|%s|%s|%d", b.Index, b.StartTime, b.EndTime, durStr, b.Project, b.Duration))
	}
	todayBlocksC := C.CString(strings.Join(blockLines, "\n"))
	defer C.free(unsafe.Pointer(todayBlocksC))

	weekSumText := fmt.Sprintf("This Week: %s total", stats.FormatDuration(summary.WeekTotalMin))
	weekSummaryC := C.CString(weekSumText)
	defer C.free(unsafe.Pointer(weekSummaryC))

	var dayLines []string
	for _, d := range summary.WeekDays {
		isToday := 0
		if d.IsToday {
			isToday = 1
		}
		dayLines = append(dayLines, fmt.Sprintf("%s|%s|%d|%s|%d", d.DayName, d.DateStr, d.Minutes, d.TimeStr, isToday))
	}
	weekDaysC := C.CString(strings.Join(dayLines, "\n"))
	defer C.free(unsafe.Pointer(weekDaysC))

	C.pom_gtk_update_stats(todaySummaryC, todayBlocksC, weekSummaryC, weekDaysC)

	allProjectsC := C.CString(strings.Join(summary.AllProjects, "\n"))
	defer C.free(unsafe.Pointer(allProjectsC))

	var projSumLines []string
	for _, ps := range summary.ProjectSummaries {
		projSumLines = append(projSumLines, fmt.Sprintf("%s|%d|%s|%d", ps.Name, ps.Minutes, ps.TimeStr, ps.SessionCount))
	}
	projSummariesC := C.CString(strings.Join(projSumLines, "\n"))
	defer C.free(unsafe.Pointer(projSummariesC))

	var pastSessLines []string
	for _, ps := range summary.PastSessions {
		durStr := stats.FormatDuration(ps.Duration)
		pastSessLines = append(pastSessLines, fmt.Sprintf("%s|%s|%s|%s|%s", ps.DateStr, ps.StartTime, ps.EndTime, durStr, ps.Project))
	}
	pastSessionsC := C.CString(strings.Join(pastSessLines, "\n"))
	defer C.free(unsafe.Pointer(pastSessionsC))

	C.pom_gtk_update_projects(allProjectsC, projSummariesC, pastSessionsC)
}

func subscribeToDaemon(outChan chan<- waybar.Output, done <-chan struct{}) {
	for {
		select {
		case <-done:
			return
		default:
		}

		conn, err := net.Dial("unix", ipc.GetSocketPath())
		if err != nil {
			time.Sleep(500 * time.Millisecond)
			continue
		}

		if _, err := fmt.Fprintln(conn, "subscribe"); err != nil {
			conn.Close()
			time.Sleep(500 * time.Millisecond)
			continue
		}

		go func() {
			<-done
			conn.Close()
		}()

		scanner := bufio.NewScanner(conn)
		for scanner.Scan() {
			var out waybar.Output
			if err := json.Unmarshal(scanner.Bytes(), &out); err == nil {
				select {
				case outChan <- out:
				case <-done:
					conn.Close()
					return
				}
			}
		}

		conn.Close()
		time.Sleep(500 * time.Millisecond)
	}
}
