package ui

import (
	"fmt"
	"image/color"

	"fyne.io/fyne/v2"
	"fyne.io/fyne/v2/canvas"
	"fyne.io/fyne/v2/container"
	"fyne.io/fyne/v2/layout"
	"fyne.io/fyne/v2/theme"
	"fyne.io/fyne/v2/widget"

	"waybar-pomodoro/internal/stats"
	"waybar-pomodoro/internal/waybar"
)

// View represents the reusable Pomodoro UI components and its state updater.
type View struct {
	Content fyne.CanvasObject
	Apply   func(state waybar.Output, summary stats.StatsSummary)
}

// barLayout draws a thin rounded progress bar: a track with a fill on top.
type barLayout struct{ value *float64 }

func (l *barLayout) Layout(o []fyne.CanvasObject, size fyne.Size) {
	o[0].Move(fyne.NewPos(0, 0))
	o[0].Resize(size)
	o[1].Move(fyne.NewPos(0, 0))
	o[1].Resize(fyne.NewSize(size.Width*float32(*l.value), size.Height))
}

func (l *barLayout) MinSize([]fyne.CanvasObject) fyne.Size { return fyne.NewSize(120, 6) }

func newText(s string, size float32, c color.Color, style fyne.TextStyle) *canvas.Text {
	t := canvas.NewText(s, c)
	t.TextSize = size
	t.TextStyle = style
	t.Alignment = fyne.TextAlignCenter
	return t
}

// BuildView creates the shared UI layout used by both the production popup and the dev preview.
func BuildView(send func(string)) *View {
	modeText := newText("FOCUS", 12, colWork, fyne.TextStyle{Bold: true})
	statusText := newText("Paused", 11, colPaused, fyne.TextStyle{})
	timerText := newText("25:00", 56, colWork, fyne.TextStyle{Bold: true, Monospace: true})

	// Progress bar
	var barValue float64
	track := canvas.NewRectangle(colSurface0)
	track.CornerRadius = 3
	fill := canvas.NewRectangle(colWork)
	fill.CornerRadius = 3
	bar := container.New(&barLayout{value: &barValue}, track, fill)

	// Cycle dots
	dots := make([]*canvas.Circle, 4)
	dotItems := make([]fyne.CanvasObject, 4)
	for i := range dots {
		dots[i] = canvas.NewCircle(colSurface1)
		dotItems[i] = container.NewGridWrap(fyne.NewSize(10, 10), dots[i])
	}
	dotRow := container.NewCenter(container.NewHBox(dotItems...))
	cycleText := newText("Cycle 1", 11, colSubtext, fyne.TextStyle{})

	todayText := newText("0 sessions · 0 min focus", 13, colText, fyne.TextStyle{})
	historyText := newText("No sessions yet today", 11, colSubtext, fyne.TextStyle{})

	toggleBtn := widget.NewButtonWithIcon("", theme.MediaPlayIcon(), func() { send("toggle") })
	toggleBtn.Importance = widget.HighImportance
	skipBtn := widget.NewButtonWithIcon("", theme.MediaSkipNextIcon(), func() { send("skip") })
	resetBtn := widget.NewButtonWithIcon("", theme.ViewRefreshIcon(), func() { send("reset") })
	stopBtn := widget.NewButtonWithIcon("", theme.MediaStopIcon(), func() { send("stop") })
	stopBtn.Importance = widget.DangerImportance

	buttonGrid := container.NewGridWithColumns(4, toggleBtn, skipBtn, resetBtn, stopBtn)

	content := container.New(
		layout.NewCustomPaddedLayout(6, 6, 12, 12),
		container.NewVBox(
			layout.NewSpacer(),
			modeText,
			timerText,
			statusText,
			bar,
			dotRow,
			cycleText,
			layout.NewSpacer(),
			widget.NewSeparator(),
			todayText,
			historyText,
			widget.NewSeparator(),
			layout.NewSpacer(),
			buttonGrid,
			layout.NewSpacer(),
		),
	)

	apply := func(state waybar.Output, summary stats.StatsSummary) {
		mc, modeName := colWork, "FOCUS"
		if state.Mode == "break" {
			mc, modeName = colBreak, "BREAK"
		}
		modeText.Text, modeText.Color = modeName, mc
		modeText.Refresh()

		if state.Running {
			statusText.Text, statusText.Color = "Running", mc
			toggleBtn.SetIcon(theme.MediaPauseIcon())
		} else {
			statusText.Text, statusText.Color = "Paused", colPaused
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
		barValue = float64(elapsed) / float64(total)
		fill.FillColor = mc
		fill.Refresh()
		bar.Refresh()

		// Cycle indicator (4 intervals per round)
		pos := summary.TodayCount % 4
		for i, d := range dots {
			if i < pos {
				d.FillColor = colWork
			} else {
				d.FillColor = colSurface1
			}
			d.Refresh()
		}
		cycleText.Text = fmt.Sprintf("Cycle %d", summary.TodayCount/4+1)
		cycleText.Refresh()

		todayText.Text = fmt.Sprintf("%d sessions · %d min focus", summary.TodayCount, summary.TodayMinutes)
		todayText.Refresh()

		if len(summary.RecentHistory) > 0 {
			rec := summary.RecentHistory[0]
			modeDesc := "Focus"
			if rec.Mode == "break" {
				modeDesc = "Break"
			}
			historyText.Text = fmt.Sprintf("Last: %s · %s (%d min)", rec.Timestamp.Local().Format("15:04"), modeDesc, rec.Duration/60)
		} else {
			historyText.Text = "No sessions yet today"
		}
		historyText.Refresh()
	}

	return &View{Content: content, Apply: apply}
}
