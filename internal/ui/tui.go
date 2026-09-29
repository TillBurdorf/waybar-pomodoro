package ui

import (
	"bufio"
	"encoding/json"
	"fmt"
	"net"
	"os"
	"os/signal"
	"strings"
	"syscall"
	"time"

	"github.com/mattn/go-runewidth"
	"golang.org/x/term"

	"waybar-pomodoro/internal/ipc"
	"waybar-pomodoro/internal/stats"
	"waybar-pomodoro/internal/waybar"
)

var digitFont = [11][3]string{
	// 0
	{"█▀▀█", "█  █", "█▄▄█"},
	// 1
	{" ▄█ ", "  █ ", " ▄█▄"},
	// 2
	{"█▀▀█", "  ▄▀", "█▄▄▄"},
	// 3
	{"█▀▀█", "  ▀▄", "█▄▄█"},
	// 4
	{"█  █", "█▄▄█", "   █"},
	// 5
	{"█▀▀▀", "▀▀▀█", "▄▄▄█"},
	// 6
	{"█▀▀█", "█▀▀▄", "▀▄▄█"},
	// 7
	{"▀▀▀█", "  █ ", "  █ "},
	// 8
	{"█▀▀█", "▄▀▀▄", "▀▄▄█"},
	// 9
	{"█▀▀█", "▀▄▄█", "   █"},
	// : (colon, index 10)
	{"   ", " ▄ ", " ▄ "},
}

func getDigitIdx(b byte) int {
	if b >= '0' && b <= '9' {
		return int(b - '0')
	}
	return 10
}

func renderDigits(mins, secs int) [3]string {
	timeStr := fmt.Sprintf("%02d:%02d", mins, secs)
	var lines [3]string
	for row := 0; row < 3; row++ {
		var line strings.Builder
		for i := 0; i < len(timeStr); i++ {
			idx := getDigitIdx(timeStr[i])
			line.WriteString(digitFont[idx][row])
			if i < len(timeStr)-1 {
				line.WriteString(" ")
			}
		}
		lines[row] = line.String()
	}
	return lines
}

func RunTUI() error {
	if err := ipc.EnsureDaemonRunning(); err != nil {
		return fmt.Errorf("failed to start daemon: %w", err)
	}

	fd := int(os.Stdin.Fd())
	if !term.IsTerminal(fd) {
		return fmt.Errorf("stdin is not a terminal")
	}

	oldState, err := term.MakeRaw(fd)
	if err != nil {
		return fmt.Errorf("failed to set raw terminal: %w", err)
	}

	cleanup := func() {
		fmt.Print("\033[?25h\033[0m\033[2J\033[H")
		_ = term.Restore(fd, oldState)
	}
	defer cleanup()

	// Hide cursor and clear screen
	fmt.Print("\033[?25l\033[2J")

	sigChan := make(chan os.Signal, 1)
	signal.Notify(sigChan, os.Interrupt, syscall.SIGTERM)

	keyChan := make(chan byte)
	go func() {
		buf := make([]byte, 1)
		for {
			n, err := os.Stdin.Read(buf)
			if err != nil || n == 0 {
				return
			}
			keyChan <- buf[0]
		}
	}()

	updateChan := make(chan waybar.Output, 16)
	go subscribeToDaemonTUI(updateChan)

	var current waybar.Output
	current.Mode = "work"
	current.Remaining = 25 * 60
	current.Total = 25 * 60

	statsSummary, _ := stats.GetStats()
	lastStatsRefresh := time.Now()

	redraw := func() {
		if time.Since(lastStatsRefresh) > 2*time.Second {
			if s, err := stats.GetStats(); err == nil {
				statsSummary = s
			}
			lastStatsRefresh = time.Now()
		}
		renderScreen(current, statsSummary)
	}

	redraw()

	for {
		select {
		case <-sigChan:
			return nil

		case k := <-keyChan:
			switch k {
			case 'q', 'Q', 27, 3: // 'q', Esc, Ctrl+C
				return nil
			case ' ':
				_ = ipc.SendCommand("toggle")
			case 's', 'S':
				_ = ipc.SendCommand("skip")
			case 'r', 'R':
				_ = ipc.SendCommand("reset")
			}

		case update := <-updateChan:
			current = update
			redraw()
		}
	}
}

func subscribeToDaemonTUI(outChan chan<- waybar.Output) {
	for {
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

		scanner := bufio.NewScanner(conn)
		for scanner.Scan() {
			var out waybar.Output
			if err := json.Unmarshal(scanner.Bytes(), &out); err == nil {
				outChan <- out
			}
		}

		conn.Close()
		time.Sleep(500 * time.Millisecond)
	}
}

func renderScreen(state waybar.Output, summary stats.StatsSummary) {
	width, height, err := term.GetSize(int(os.Stdout.Fd()))
	if err != nil || width < 40 {
		width = 46
	}
	if height < 18 {
		height = 20
	}

	boxWidth := width
	if boxWidth > 47 {
		boxWidth = 47
	}
	if boxWidth < 44 {
		boxWidth = 44
	}
	leftMargin := (width - boxWidth) / 2
	if leftMargin < 0 {
		leftMargin = 0
	}

	var sb strings.Builder
	sb.WriteString("\033[H")

	// Catppuccin palette
	const (
		cReset   = "\033[0m"
		cBold    = "\033[1m"
		cBorder  = "\033[38;2;108;112;134m" // Surface2
		cTitle   = "\033[1;38;2;180;190;254m" // Lavender
		cWork    = "\033[1;38;2;243;139;168m" // Maroon / Peach
		cBreak   = "\033[1;38;2;166;227;161m" // Green
		cRunning = "\033[1;38;2;166;227;161m" // Green
		cPaused  = "\033[1;38;2;249;226;175m" // Yellow
		cBarFill = "\033[38;2;137;180;250m"   // Blue
		cBarDim  = "\033[38;2;69;71;90m"     // Surface1
		cText    = "\033[38;2;205;214;244m"   // Text
		cDim     = "\033[38;2;147;153;178m"   // Subtext0
		cKey     = "\033[1;38;2;137;220;235m" // Sky
	)

	// Box drawing helpers with exact terminal column width calculation
	boxRow := func(visibleWidth int, content string) {
		pad := boxWidth - 2 - visibleWidth
		if pad < 0 {
			pad = 0
		}
		sb.WriteString(strings.Repeat(" ", leftMargin) + cBorder + "│" + cReset + content + strings.Repeat(" ", pad) + cBorder + "│\r\n")
	}

	boxCenteredRow := func(visibleWidth int, content string) {
		totalPad := boxWidth - 2 - visibleWidth
		if totalPad < 0 {
			totalPad = 0
		}
		leftPad := totalPad / 2
		rightPad := totalPad - leftPad
		sb.WriteString(strings.Repeat(" ", leftMargin) + cBorder + "│" + cReset + strings.Repeat(" ", leftPad) + content + strings.Repeat(" ", rightPad) + cBorder + "│\r\n")
	}

	boxDivider := func() {
		sb.WriteString(strings.Repeat(" ", leftMargin) + cBorder + "├" + strings.Repeat("─", boxWidth-2) + "┤\r\n")
	}

	// 1. Top border
	sb.WriteString(strings.Repeat(" ", leftMargin) + cBorder + "╭" + strings.Repeat("─", boxWidth-2) + "╮\r\n")

	// 2. Title
	titleRaw := "🍅  POMODORO DASHBOARD"
	boxCenteredRow(runewidth.StringWidth(titleRaw), cTitle+titleRaw+cReset)

	// 3. Divider
	boxDivider()

	// 4. Mode
	modeBadge := "[ FOCUS SESSION 🍅 ]"
	modeColor := cWork
	if state.Mode == "break" {
		modeBadge = "[ SHORT BREAK ☕ ]"
		modeColor = cBreak
	}
	modeRaw := "  Mode:   " + modeBadge
	boxRow(runewidth.StringWidth(modeRaw), "  "+cDim+"Mode:   "+modeColor+modeBadge+cReset)

	// 5. Status
	statusBadge := "● RUNNING"
	statusColor := cRunning
	if !state.Running {
		statusBadge = "⏸ PAUSED"
		statusColor = cPaused
	}
	statusRaw := "  Status: " + statusBadge
	boxRow(runewidth.StringWidth(statusRaw), "  "+cDim+"Status: "+statusColor+statusBadge+cReset)

	// 6. Empty line
	boxRow(0, "")

	// 7. Digital Clock
	mins := state.Remaining / 60
	secs := state.Remaining % 60
	digits := renderDigits(mins, secs)
	clockWidth := runewidth.StringWidth(digits[0])

	for _, dl := range digits {
		boxCenteredRow(clockWidth, modeColor+dl+cReset)
	}

	// 8. Empty line
	boxRow(0, "")

	// 9. Progress Bar
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
	percent := (elapsed * 100) / total

	barInnerWidth := boxWidth - 14
	if barInnerWidth < 10 {
		barInnerWidth = 10
	}
	filled := (elapsed * barInnerWidth) / total
	empty := barInnerWidth - filled

	barRaw := fmt.Sprintf("[%s%s] %3d%%", strings.Repeat("█", filled), strings.Repeat("░", empty), percent)
	barContent := fmt.Sprintf("[%s%s%s%s%s] %s%3d%%%s",
		cBarFill, strings.Repeat("█", filled),
		cBarDim, strings.Repeat("░", empty),
		cReset, cBold+cText, percent, cReset)
	boxCenteredRow(runewidth.StringWidth(barRaw), barContent)

	// 10. Divider
	boxDivider()

	// 11. Today's Summary
	todayRaw := fmt.Sprintf(" 📊 Today: %d 🍅  •  %d min focus", summary.TodayCount, summary.TodayMinutes)
	todayContent := fmt.Sprintf(" %s📊 Today:%s %d 🍅  •  %d min focus", cTitle, cReset, summary.TodayCount, summary.TodayMinutes)
	boxRow(runewidth.StringWidth(todayRaw), todayContent)

	// 12. Divider
	boxDivider()

	// 13. Recent Sessions
	if len(summary.RecentHistory) == 0 {
		recRaw := " 📜 Recent: No sessions yet today."
		recContent := fmt.Sprintf(" %s📜 Recent:%s %sNo sessions yet today.%s", cTitle, cReset, cDim, cReset)
		boxRow(runewidth.StringWidth(recRaw), recContent)
	} else {
		rec := summary.RecentHistory[0]
		tStr := rec.Timestamp.Local().Format("15:04")
		durMin := rec.Duration / 60
		modeName := "Work"
		if rec.Mode == "break" {
			modeName = "Break"
		}
		recRaw := fmt.Sprintf(" 📜 Last: %s - %s (%d min)", tStr, modeName, durMin)
		recContent := fmt.Sprintf(" %s📜 Last:%s %s%s - %s (%d min)%s", cTitle, cReset, cDim, tStr, modeName, durMin, cReset)
		boxRow(runewidth.StringWidth(recRaw), recContent)
	}

	// 14. Divider
	boxDivider()

	// 15. Controls
	ctrlRaw := "[Space] Toggle  [s] Skip  [r] Reset  [q] Quit"
	ctrlContent := cKey + "[Space]" + cDim + " Toggle  " +
		cKey + "[s]" + cDim + " Skip  " +
		cKey + "[r]" + cDim + " Reset  " +
		cKey + "[q]" + cDim + " Quit" + cReset
	boxCenteredRow(runewidth.StringWidth(ctrlRaw), ctrlContent)

	// 16. Bottom border
	sb.WriteString(strings.Repeat(" ", leftMargin) + cBorder + "╰" + strings.Repeat("─", boxWidth-2) + "╯\r\n" + cReset)

	os.Stdout.WriteString(sb.String())
}
