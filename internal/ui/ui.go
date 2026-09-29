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

	"waybar-pomodoro/internal/ipc"
	"waybar-pomodoro/internal/stats"
	"waybar-pomodoro/internal/waybar"
)

//export goGTKCommand
func goGTKCommand(command *C.char) {
	cmd := C.GoString(command)
	go func() { _ = ipc.SendCommand(cmd) }()
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
		lastStats := time.Now()
		for {
			select {
			case <-done:
				return
			case state := <-updates:
				if time.Since(lastStats) > 2*time.Second {
					if current, err := stats.GetStats(); err == nil {
						summary = current
					}
					lastStats = time.Now()
				}
				updateGTK(state, summary)
			}
		}
	}()

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

func updateGTK(state waybar.Output, summary stats.StatsSummary) {
	modeName := "FOCUS"
	if state.Mode == "break" {
		modeName = "BREAK"
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
		}
	}
	elapsed := total - state.Remaining
	if elapsed < 0 {
		elapsed = 0
	} else if elapsed > total {
		elapsed = total
	}
	fraction := float64(elapsed) / float64(total)
	cycle := summary.TodayCount/4 + 1
	dots := strings.Repeat("●  ", summary.TodayCount%4) + strings.Repeat("○  ", 4-summary.TodayCount%4)
	cycleText := fmt.Sprintf("%s Cycle %d", strings.TrimSpace(dots), cycle)
	history := "No sessions yet today"
	if len(summary.RecentHistory) > 0 {
		record := summary.RecentHistory[0]
		phase := "Focus"
		if record.Mode == "break" {
			phase = "Break"
		}
		history = fmt.Sprintf("Last: %s · %s (%d min)", record.Timestamp.Local().Format("15:04"), phase, record.Duration/60)
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
