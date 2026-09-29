package ui

import (
	"bufio"
	"encoding/json"
	"fmt"
	"net"
	"os"
	"os/signal"
	"strconv"
	"strings"
	"sync"
	"syscall"
	"time"

	"fyne.io/fyne/v2"
	"fyne.io/fyne/v2/app"
	"fyne.io/fyne/v2/container"

	"waybar-pomodoro/internal/ipc"
	"waybar-pomodoro/internal/stats"
	"waybar-pomodoro/internal/waybar"
)



func getUIPidPath() string {
	return fmt.Sprintf("/tmp/waybar-pomodoro-%d-ui.pid", os.Getuid())
}

// toggleOrAcquireSingleInstance checks if an existing UI window is already open.
// If active, it sends SIGTERM so repeated Waybar clicks dismiss the popup cleanly.
func toggleOrAcquireSingleInstance() (bool, error) {
	pidPath := getUIPidPath()
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

func RunUI() error {
	toggledOff, err := toggleOrAcquireSingleInstance()
	if err != nil {
		return fmt.Errorf("failed single-instance check: %w", err)
	}
	if toggledOff {
		return nil
	}
	pidPath := getUIPidPath()
	defer os.Remove(pidPath)

	if err := ipc.EnsureDaemonRunning(); err != nil {
		return fmt.Errorf("failed to start daemon: %w", err)
	}

	if os.Getenv("FYNE_SCALE") == "" {
		_ = os.Setenv("FYNE_SCALE", "1")
	}

	a := app.NewWithID("waybar-pomodoro")
	a.Settings().SetTheme(NewMochaTheme())

	w := a.NewWindow("Pomodoro")
	w.SetFixedSize(true)

	// Dismiss window cleanly upon SIGTERM (e.g. from subsequent click or compositor)
	sigChan := make(chan os.Signal, 1)
	signal.Notify(sigChan, os.Interrupt, syscall.SIGTERM)
	go func() {
		<-sigChan
		_ = os.Remove(pidPath)
		a.Quit()
	}()

	send := func(cmd string) {
		go func() { _ = ipc.SendCommand(cmd) }()
	}

	view := BuildView(send)
	w.SetContent(container.NewPadded(view.Content))
	w.Resize(fyne.NewSize(320, 360))

	// --- keyboard shortcuts ---
	w.Canvas().SetOnTypedKey(func(e *fyne.KeyEvent) {
		switch e.Name {
		case fyne.KeySpace:
			send("toggle")
		case fyne.KeyEscape:
			a.Quit()
		}
	})
	w.Canvas().SetOnTypedRune(func(r rune) {
		switch r {
		case 's', 'S':
			send("skip")
		case 'r', 'R':
			send("reset")
		case 'x', 'X':
			send("stop")
		case 'q', 'Q':
			a.Quit()
		}
	})

	apply := view.Apply

	// --- daemon subscription ---
	done := make(chan struct{})
	var once sync.Once
	stop := func() {
		once.Do(func() {
			close(done)
			_ = os.Remove(pidPath)
		})
	}
	w.SetOnClosed(stop)

	updateChan := make(chan waybar.Output, 16)
	go subscribeToDaemon(updateChan, done)

	current := waybar.Output{Mode: "work", Remaining: 25 * 60, Total: 25 * 60}
	summary, _ := stats.GetStats()
	apply(current, summary)

	go func() {
		lastStats := time.Now()
		for {
			select {
			case <-done:
				return
			case upd := <-updateChan:
				if time.Since(lastStats) > 2*time.Second {
					if s, err := stats.GetStats(); err == nil {
						summary = s
					}
					lastStats = time.Now()
				}
				st, sm := upd, summary
				fyne.Do(func() { apply(st, sm) })
			}
		}
	}()

	w.ShowAndRun()
	stop()
	return nil
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
