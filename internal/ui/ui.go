package ui

import (
	"bufio"
	"encoding/json"
	"fmt"
	"image/color"
	"net"
	"os"
	"sync"
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

func RunUI() error {
	if err := ipc.EnsureDaemonRunning(); err != nil {
		return fmt.Errorf("failed to start daemon: %w", err)
	}

	if os.Getenv("FYNE_SCALE") == "" {
		_ = os.Setenv("FYNE_SCALE", "1")
	}

	a := app.NewWithID("waybar-pomodoro")
	a.Settings().SetTheme(pomodoroTheme{theme.DefaultTheme()})

	w := a.NewWindow("Pomodoro")

	// --- widgets ---
	modeText := canvas.NewText("FOCUS 🍅", colWork)
	modeText.TextSize = 13
	modeText.TextStyle = fyne.TextStyle{Bold: true}
	modeText.Alignment = fyne.TextAlignCenter

	statusText := canvas.NewText("⏸ Paused", colPaused)
	statusText.TextSize = 11
	statusText.Alignment = fyne.TextAlignCenter

	timerText := canvas.NewText("25:00", colWork)
	timerText.TextSize = 52
	timerText.TextStyle = fyne.TextStyle{Bold: true, Monospace: true}
	timerText.Alignment = fyne.TextAlignCenter

	progress := widget.NewProgressBar()
	progress.Min, progress.Max = 0, 1
	progress.TextFormatter = func() string { return "" }

	todayLabel := widget.NewLabel("0 🍅 • 0 min")
	todayLabel.Alignment = fyne.TextAlignCenter

	send := func(cmd string) {
		go func() { _ = ipc.SendCommand(cmd) }()
	}

	toggleBtn := widget.NewButtonWithIcon("", theme.MediaPlayIcon(), func() { send("toggle") })
	toggleBtn.Importance = widget.HighImportance
	skipBtn := widget.NewButtonWithIcon("", theme.MediaSkipNextIcon(), func() { send("skip") })
	resetBtn := widget.NewButtonWithIcon("", theme.ViewRefreshIcon(), func() { send("reset") })

	content := container.NewVBox(
		modeText,
		statusText,
		timerText,
		progress,
		todayLabel,
		container.NewGridWithColumns(3, toggleBtn, skipBtn, resetBtn),
	)
	w.SetContent(container.NewPadded(content))
	w.Resize(fyne.NewSize(320, 360))

	// --- keyboard shortcuts (same keys as the TUI) ---
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
		case 'q', 'Q':
			a.Quit()
		}
	})

	// --- state rendering (must run on the UI thread) ---
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

		todayLabel.SetText(fmt.Sprintf("%d 🍅 • %d min", summary.TodayCount, summary.TodayMinutes))
	}

	// --- daemon subscription ---
	done := make(chan struct{})
	var once sync.Once
	stop := func() { once.Do(func() { close(done) }) }
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

		// Close the connection when the window closes so Scan unblocks.
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
