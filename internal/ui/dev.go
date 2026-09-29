package ui

import (
	"fmt"
	"image/color"
	"os"
	"os/exec"
	"path/filepath"
	"plugin"
	"strings"
	"sync"
	"time"

	"fyne.io/fyne/v2"
	"fyne.io/fyne/v2/app"
	"fyne.io/fyne/v2/canvas"
	"fyne.io/fyne/v2/container"
	"fyne.io/fyne/v2/widget"

	"waybar-pomodoro/internal/stats"
	"waybar-pomodoro/internal/waybar"
)

// buildErrorCard creates a styled error display fitting inside the 320x360 preview card.
func buildErrorCard(errText string) fyne.CanvasObject {
	header := canvas.NewText("❌ BUILD ERROR", colWork)
	header.TextSize = 13
	header.TextStyle = fyne.TextStyle{Bold: true}
	header.Alignment = fyne.TextAlignCenter

	sub := canvas.NewText("Live reload paused • Fix code to recover", colSubtext)
	sub.TextSize = 10
	sub.Alignment = fyne.TextAlignCenter

	entry := widget.NewMultiLineEntry()
	entry.SetText(errText)
	entry.TextStyle = fyne.TextStyle{Monospace: true}
	entry.Wrapping = fyne.TextWrapWord
	entry.Disable()

	bottom := canvas.NewText("Watching internal/ui for changes...", colSubtext)
	bottom.TextSize = 9
	bottom.Alignment = fyne.TextAlignCenter

	return container.NewBorder(
		container.NewVBox(header, sub, widget.NewSeparator()),
		container.NewVBox(widget.NewSeparator(), bottom),
		nil, nil,
		entry,
	)
}

// RunDevUI launches an isolated mock UI with interactive test hotkeys and instant live-reloading.
// It opens ONCE as a standard tiled window arranged by Hyprland and never closes or reopens across code edits or errors.
func RunDevUI() error {
	if os.Getenv("FYNE_SCALE") == "" {
		_ = os.Setenv("FYNE_SCALE", "1")
	}

	a := app.NewWithID("waybar-pomodoro-dev")
	a.Settings().SetTheme(NewMochaTheme())

	w := a.NewWindow("Pomodoro [DEV PREVIEW]")

	// --- Mock State ---
	state := waybar.Output{
		Mode:      "work",
		Remaining: 15 * 60,
		Total:     25 * 60,
		Running:   true,
	}
	summary := stats.StatsSummary{
		TodayCount:   3,
		TodayMinutes: 75,
		RecentHistory: []stats.SessionRecord{
			{Timestamp: time.Now().Add(-20 * time.Minute), Mode: "work", Duration: 1500},
		},
	}

	var mu sync.Mutex
	var currentApply func(waybar.Output, stats.StatsSummary)

	mockSend := func(cmd string) {
		mu.Lock()
		defer mu.Unlock()
		switch cmd {
		case "toggle":
			state.Running = !state.Running
		case "skip":
			if state.Mode == "work" {
				state.Mode = "break"
				state.Total = 5 * 60
				state.Remaining = 5 * 60
			} else {
				state.Mode = "work"
				state.Total = 25 * 60
				state.Remaining = 25 * 60
			}
			state.Running = false
		case "reset":
			if state.Mode == "break" {
				state.Remaining = 5 * 60
			} else {
				state.Remaining = 25 * 60
			}
			state.Running = false
		case "stop":
			state.Mode = "work"
			state.Total = 25 * 60
			state.Remaining = 25 * 60
			state.Running = false
		}
		if currentApply != nil {
			st, sm := state, summary
			fyne.Do(func() { currentApply(st, sm) })
		}
	}

	// Initial synchronous build using static code
	initialView := BuildView(mockSend)
	currentApply = initialView.Apply

	// Fixed-size card (320x360) simulating the exact floating window look & feel
	cardBg := canvas.NewRectangle(colBase)
	cardBg.SetMinSize(fyne.NewSize(320, 360))
	cardBg.CornerRadius = 12
	cardBg.StrokeColor = colSurface1
	cardBg.StrokeWidth = 1.5

	innerCard := container.NewStack(initialView.Content)
	card := container.NewStack(cardBg, container.NewPadded(innerCard))

	topHint := canvas.NewText("DEV PREVIEW  •  320×360 Floating Window Simulation (Live Reload)", colSubtext)
	topHint.TextSize = 11
	topHint.Alignment = fyne.TextAlignCenter

	bottomHint := canvas.NewText("[M] Mode  •  [P] Progress  •  [C] Cycles  •  [+/-] Time  •  [Space] Play/Pause", colSubtext)
	bottomHint.TextSize = 10
	bottomHint.Alignment = fyne.TextAlignCenter

	backdrop := canvas.NewRectangle(color.NRGBA{17, 17, 27, 255}) // Catppuccin Crust

	devLayout := container.NewBorder(
		container.NewPadded(topHint),
		container.NewPadded(bottomHint),
		nil, nil,
		container.NewCenter(card),
	)

	w.SetContent(container.NewStack(backdrop, devLayout))

	render := func() {
		mu.Lock()
		defer mu.Unlock()
		if currentApply != nil {
			st, sm := state, summary
			fyne.Do(func() {
				defer func() {
					if r := recover(); r != nil {
						innerCard.Objects = []fyne.CanvasObject{buildErrorCard(fmt.Sprintf("Runtime UI Panic:\n\n%v", r))}
						innerCard.Refresh()
					}
				}()
				currentApply(st, sm)
			})
		}
	}
	render()

	// --- Interactive Test Controls ---
	w.Canvas().SetOnTypedKey(func(e *fyne.KeyEvent) {
		switch e.Name {
		case fyne.KeySpace:
			mockSend("toggle")
		case fyne.KeyPlus, fyne.KeyEqual:
			mu.Lock()
			if state.Remaining < state.Total {
				state.Remaining += 60
			}
			mu.Unlock()
			render()
		case fyne.KeyMinus:
			mu.Lock()
			if state.Remaining > 60 {
				state.Remaining -= 60
			}
			mu.Unlock()
			render()
		case fyne.KeyEscape:
			a.Quit()
		}
	})

	w.Canvas().SetOnTypedRune(func(r rune) {
		switch r {
		case 'm', 'M':
			mu.Lock()
			if state.Mode == "work" {
				state.Mode = "break"
				state.Total = 5 * 60
				state.Remaining = 5 * 60
			} else {
				state.Mode = "work"
				state.Total = 25 * 60
				state.Remaining = 25 * 60
			}
			mu.Unlock()
			render()
		case 'p', 'P':
			mu.Lock()
			remFraction := float64(state.Remaining) / float64(state.Total)
			if remFraction > 0.75 {
				state.Remaining = int(float64(state.Total) * 0.5)
			} else if remFraction > 0.4 {
				state.Remaining = int(float64(state.Total) * 0.25)
			} else if remFraction > 0.1 {
				state.Remaining = 5
			} else {
				state.Remaining = state.Total
			}
			mu.Unlock()
			render()
		case 'c', 'C':
			mu.Lock()
			summary.TodayCount = (summary.TodayCount + 1) % 8
			summary.TodayMinutes = summary.TodayCount * 25
			mu.Unlock()
			render()
		case 's', 'S':
			mockSend("skip")
		case 'r', 'R':
			mockSend("reset")
		case 'x', 'X':
			mockSend("stop")
		case 'q', 'Q':
			a.Quit()
		}
	})

	// Local demo tick loop
	done := make(chan struct{})
	w.SetOnClosed(func() { close(done) })

	go func() {
		ticker := time.NewTicker(1 * time.Second)
		defer ticker.Stop()
		for {
			select {
			case <-done:
				return
			case <-ticker.C:
				mu.Lock()
				if state.Running {
					if state.Remaining > 0 {
						state.Remaining--
					} else {
						if state.Mode == "work" {
							state.Mode = "break"
							state.Total = 5 * 60
							state.Remaining = 5 * 60
							summary.TodayCount++
							summary.TodayMinutes += 25
						} else {
							state.Mode = "work"
							state.Total = 25 * 60
							state.Remaining = 25 * 60
						}
					}
					st, sm := state, summary
					apply := currentApply
					if apply != nil {
						fyne.Do(func() { apply(st, sm) })
					}
				}
				mu.Unlock()
			}
		}
	}()

	// --- Hot-Reload Watcher & Plugin Compiler ---
	go watchAndHotReload(done, mockSend, func(newContent fyne.CanvasObject, newApply func(waybar.Output, stats.StatsSummary)) {
		mu.Lock()
		currentApply = newApply
		st, sm := state, summary
		mu.Unlock()

		fyne.Do(func() {
			innerCard.Objects = []fyne.CanvasObject{newContent}
			innerCard.Refresh()
			if newApply != nil {
				newApply(st, sm)
			}
		})
		_ = exec.Command("notify-send", "-u", "low", "-a", "Pomodoro Dev", "UI Preview Recovered", "Preview updated in place").Run()
	}, func(errText string) {
		mu.Lock()
		currentApply = nil
		mu.Unlock()

		fyne.Do(func() {
			innerCard.Objects = []fyne.CanvasObject{buildErrorCard(errText)}
			innerCard.Refresh()
		})
		_ = exec.Command("notify-send", "-u", "critical", "-a", "Pomodoro Dev", "UI Build Error", "Check preview window for details").Run()
	})

	w.ShowAndRun()
	return nil
}

// watchAndHotReload monitors internal/ui/ for code changes, builds a plugin on the fly,
// and notifies the host window without ever destroying the Wayland surface.
func watchAndHotReload(
	done <-chan struct{},
	mockSend func(string),
	onSuccess func(fyne.CanvasObject, func(waybar.Output, stats.StatsSummary)),
	onError func(string),
) {
	fmt.Println("🍅 UI Dev Server Active — Window will remain open permanently across edits & errors.")
	lastMod := getLatestModTime()

	ticker := time.NewTicker(200 * time.Millisecond)
	defer ticker.Stop()

	for {
		select {
		case <-done:
			return
		case <-ticker.C:
			currMod := getLatestModTime()
			if currMod.After(lastMod) {
				// Debounce rapid editor writes
				time.Sleep(100 * time.Millisecond)
				lastMod = getLatestModTime()

				fmt.Println("⚡ Change detected in internal/ui — compiling preview...")

				content, apply, err := compileAndLoadPlugin(mockSend)
				if err != nil {
					fmt.Fprintf(os.Stderr, "❌ Build Error:\n%s\n", err.Error())
					onError(err.Error())
				} else {
					fmt.Println("✅ Build Succeeded — preview updated in place!")
					onSuccess(content, apply)
				}
			}
		}
	}
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

func getLatestModTime() time.Time {
	var latest time.Time
	root := findModuleRoot()
	entries, err := os.ReadDir(filepath.Join(root, "internal/ui"))
	if err != nil {
		return latest
	}
	for _, entry := range entries {
		if strings.HasSuffix(entry.Name(), ".go") {
			if info, err := entry.Info(); err == nil {
				if info.ModTime().After(latest) {
					latest = info.ModTime()
				}
			}
		}
	}
	return latest
}

func compileAndLoadPlugin(mockSend func(string)) (fyne.CanvasObject, func(waybar.Output, stats.StatsSummary), error) {
	root := findModuleRoot()
	tmpBase := filepath.Join(root, "tmp")
	_ = os.MkdirAll(tmpBase, 0o755)

	// Create a unique temporary directory for this build
	tmpDir, err := os.MkdirTemp(tmpBase, "dev_plugin_*")
	if err != nil {
		return nil, nil, fmt.Errorf("failed to create temp build dir: %w", err)
	}
	defer os.RemoveAll(tmpDir)

	uiDir := filepath.Join(root, "internal/ui")
	entries, err := os.ReadDir(uiDir)
	if err != nil {
		return nil, nil, fmt.Errorf("failed to read internal/ui: %w", err)
	}

	for _, entry := range entries {
		name := entry.Name()
		if !strings.HasSuffix(name, ".go") || strings.HasSuffix(name, "_test.go") {
			continue
		}
		if name == "dev.go" || name == "ui.go" || name == "error.go" {
			continue
		}

		data, err := os.ReadFile(filepath.Join(uiDir, name))
		if err != nil {
			return nil, nil, fmt.Errorf("failed to read %s: %w", name, err)
		}

		// Replace "package ui" with "package main"
		content := strings.Replace(string(data), "package ui", "package main", 1)
		if err := os.WriteFile(filepath.Join(tmpDir, name), []byte(content), 0o600); err != nil {
			return nil, nil, fmt.Errorf("failed to write %s: %w", name, err)
		}
	}

	// Write entry point for the plugin
	entryCode := `package main

import (
	"fyne.io/fyne/v2"
	"waybar-pomodoro/internal/stats"
	"waybar-pomodoro/internal/waybar"
)

func ExportedBuildView(send func(string)) (fyne.CanvasObject, func(waybar.Output, stats.StatsSummary)) {
	v := BuildView(send)
	return v.Content, v.Apply
}
`
	if err := os.WriteFile(filepath.Join(tmpDir, "plugin_entry.go"), []byte(entryCode), 0o600); err != nil {
		return nil, nil, fmt.Errorf("failed to write entry.go: %w", err)
	}

	soPath := filepath.Join(tmpDir, "preview.so")
	cmd := exec.Command("go", "build", "-buildmode=plugin", "-o", soPath, ".")
	cmd.Dir = tmpDir
	out, err := cmd.CombinedOutput()
	if err != nil {
		return nil, nil, fmt.Errorf("%s", strings.TrimSpace(string(out)))
	}

	// Load the compiled plugin
	p, err := plugin.Open(soPath)
	if err != nil {
		return nil, nil, fmt.Errorf("failed to load plugin: %w", err)
	}

	sym, err := p.Lookup("ExportedBuildView")
	if err != nil {
		return nil, nil, fmt.Errorf("failed to lookup ExportedBuildView: %w", err)
	}

	fn, ok := sym.(func(func(string)) (fyne.CanvasObject, func(waybar.Output, stats.StatsSummary)))
	if !ok {
		return nil, nil, fmt.Errorf("invalid symbol signature in compiled plugin")
	}

	var content fyne.CanvasObject
	var apply func(waybar.Output, stats.StatsSummary)
	var panicErr error

	func() {
		defer func() {
			if r := recover(); r != nil {
				panicErr = fmt.Errorf("panic in BuildView: %v", r)
			}
		}()
		content, apply = fn(mockSend)
	}()

	if panicErr != nil {
		return nil, nil, panicErr
	}

	return content, apply, nil
}
