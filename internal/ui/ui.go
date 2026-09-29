package ui

import (
	"bufio"
	"encoding/json"
	"fmt"
	"image/color"
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
	"fyne.io/fyne/v2/canvas"
	"fyne.io/fyne/v2/container"
	"fyne.io/fyne/v2/theme"
	"fyne.io/fyne/v2/widget"

	"waybar-pomodoro/internal/ipc"
	"waybar-pomodoro/internal/stats"
	"waybar-pomodoro/internal/waybar"
)

// Catppuccin Mocha palette
var (
	colBase     = color.NRGBA{30, 30, 46, 255}
	colSurface0 = color.NRGBA{49, 50, 68, 255}
	colSurface1 = color.NRGBA{69, 71, 90, 255}
	colText     = color.NRGBA{205, 214, 244, 255}
	colSubtext  = color.NRGBA{147, 153, 178, 255}
	colBlue     = color.NRGBA{137, 180, 250, 255}
	colWork     = color.NRGBA{243, 139, 168, 255}
	colBreak    = color.NRGBA{166, 227, 161, 255}
	colPaused   = color.NRGBA{249, 226, 175, 255}
)

type pomodoroTheme struct{ fyne.Theme }

func (t pomodoroTheme) Color(n fyne.ThemeColorName, _ fyne.ThemeVariant) color.Color {
	switch n {
	case theme.ColorNameBackground:
		return colBase
	case theme.ColorNameForeground:
		return colText
	case theme.ColorNamePrimary:
		return colBlue
	case theme.ColorNameButton, theme.ColorNameInputBackground:
		return colSurface0
	case theme.ColorNameDisabled, theme.ColorNamePlaceHolder:
		return colSubtext
	case theme.ColorNameSeparator:
		return colSurface1
	}
	return t.Theme.Color(n, theme.VariantDark)
}

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
	a.Settings().SetTheme(pomodoroTheme{theme.DefaultTheme()})

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

	// --- widgets ---
	modeText := canvas.NewText("FOCUS 🍅", colWork)
	modeText.TextSize = 13
	modeText.TextStyle = fyne.TextStyle{Bold: true}
	modeText.Alignment = fyne.TextAlignCenter

	statusText := canvas.NewText("⏸ Paused", colPaused)
	statusText.TextSize = 11
	statusText.Alignment = fyne.TextAlignCenter

	timerText := canvas.NewText("25:00", colWork)
	timerText.TextSize = 48
	timerText.TextStyle = fyne.TextStyle{Bold: true, Monospace: true}
	timerText.Alignment = fyne.TextAlignCenter

	progress := widget.NewProgressBar()
	progress.Min, progress.Max = 0, 1
	progress.TextFormatter = func() string {
		return fmt.Sprintf("%.0f%%", progress.Value*100)
	}

	cycleText := canvas.NewText("○ ○ ○ ○  •  Cycle 1", colSubtext)
	cycleText.TextSize = 12
	cycleText.TextStyle = fyne.TextStyle{Bold: true}
	cycleText.Alignment = fyne.TextAlignCenter

	todayLabel := widget.NewLabel("Today: 0 🍅 • 0 min focus")
	todayLabel.Alignment = fyne.TextAlignCenter

	historyLabel := widget.NewLabel("Last: No sessions yet today")
	historyLabel.Importance = widget.LowImportance
	historyLabel.Alignment = fyne.TextAlignCenter

	helpLabel := canvas.NewText("[Space] Toggle  •  [S] Skip  •  [R] Reset  •  [X] Stop", colSubtext)
	helpLabel.TextSize = 9
	helpLabel.Alignment = fyne.TextAlignCenter

	send := func(cmd string) {
		go func() { _ = ipc.SendCommand(cmd) }()
	}

	toggleBtn := widget.NewButtonWithIcon("", theme.MediaPlayIcon(), func() { send("toggle") })
	toggleBtn.Importance = widget.HighImportance

	skipBtn := widget.NewButtonWithIcon("", theme.MediaSkipNextIcon(), func() { send("skip") })
	resetBtn := widget.NewButtonWithIcon("", theme.ViewRefreshIcon(), func() { send("reset") })

	stopBtn := widget.NewButtonWithIcon("", theme.MediaStopIcon(), func() { send("stop") })
	stopBtn.Importance = widget.DangerImportance

	buttonGrid := container.NewGridWithColumns(4, toggleBtn, skipBtn, resetBtn, stopBtn)

	content := container.NewVBox(
		modeText,
		statusText,
		timerText,
		progress,
		cycleText,
		widget.NewSeparator(),
		todayLabel,
		historyLabel,
		widget.NewSeparator(),
		buttonGrid,
		helpLabel,
	)
	w.SetContent(container.NewPadded(content))
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

	// --- state rendering ---
	apply := func(state waybar.Output, summary stats.StatsSummary) {
		mc := colWork
		modeName := "FOCUS 🍅"
		if state.Mode == "break" {
			mc = colBreak
			modeName = "BREAK ☕"
		}
		modeText.Text, modeText.Color = modeName, mc
		modeText.Refresh()

		if state.Running {
			statusText.Text, statusText.Color = "● Running", colBreak
			toggleBtn.SetIcon(theme.MediaPauseIcon())
		} else {
			statusText.Text, statusText.Color = "⏸ Paused", colPaused
			toggleBtn.SetIcon(theme.MediaPlayIcon())
		}
		statusText.Refresh()

		timerText.Text = fmt.Sprintf("%02d:%02d", state.Remaining/60, state.Remaining%60)
		timerText.Color = mc
		timerText.Refresh()

		total := state.Total
		if total <= 0 {
			if state.Mode == "break" {
				total = 5 * 60
			} else {
				total = 25 * 60
			}
		}
		elapsed := total - state.Remaining
		if elapsed < 0 {
			elapsed = 0
		} else if elapsed > total {
			elapsed = total
		}
		progress.SetValue(float64(elapsed) / float64(total))

		// Cycle indicator (4 intervals per round)
		cycleNum := (summary.TodayCount / 4) + 1
		pos := summary.TodayCount % 4
		var dots strings.Builder
		for i := 0; i < 4; i++ {
			if i < pos {
				dots.WriteString("🍅 ")
			} else {
				dots.WriteString("○ ")
			}
		}
		cycleText.Text = fmt.Sprintf("%s •  Cycle %d", strings.TrimSpace(dots.String()), cycleNum)
		cycleText.Refresh()

		todayLabel.SetText(fmt.Sprintf("Today: %d 🍅 • %d min focus", summary.TodayCount, summary.TodayMinutes))

		if len(summary.RecentHistory) > 0 {
			rec := summary.RecentHistory[0]
			modeDesc := "Focus"
			if rec.Mode == "break" {
				modeDesc = "Break"
			}
			historyLabel.SetText(fmt.Sprintf("Last: %s • %s (%d min)", rec.Timestamp.Local().Format("15:04"), modeDesc, rec.Duration/60))
		} else {
			historyLabel.SetText("Last: No sessions yet today")
		}
	}

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
