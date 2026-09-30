package timer

import (
	"bufio"
	"fmt"
	"log"
	"net"
	"os"
	"os/exec"
	"os/signal"
	"strings"
	"sync"
	"syscall"
	"time"

	"waybar-pomodoro/internal/config"
	"waybar-pomodoro/internal/ipc"
	"waybar-pomodoro/internal/stats"
	"waybar-pomodoro/internal/waybar"
)

type Timer struct {
	mu                sync.Mutex
	mode              string // "work", "break", "long_break"
	remaining         int    // seconds
	workDuration      int    // seconds
	breakDuration     int    // seconds
	longBreakDuration int    // seconds
	totalCycles       int    // number of circles per set
	completedCycles   int    // completed sessions in current set
	running           bool
	subscribers       map[net.Conn]struct{}
}

func New() *Timer {
	cfg := config.Load()
	w := cfg.WorkDurationMinutes * 60
	b := cfg.BreakDurationMinutes * 60
	lb := cfg.LongBreakDurationMinutes * 60
	tc := cfg.TotalCycles
	if tc <= 0 {
		tc = 4
	}
	return &Timer{
		mode:              "work",
		remaining:         w,
		workDuration:      w,
		breakDuration:     b,
		longBreakDuration: lb,
		totalCycles:       tc,
		completedCycles:   0,
		running:           false,
		subscribers:       make(map[net.Conn]struct{}),
	}
}

func (t *Timer) totalLocked() int {
	switch t.mode {
	case "long_break":
		return t.longBreakDuration
	case "break":
		return t.breakDuration
	default:
		return t.workDuration
	}
}

func (t *Timer) broadcastLocked() {
	out := waybar.Format(t.mode, t.remaining, t.totalLocked(), t.running)
	for conn := range t.subscribers {
		_ = conn.SetWriteDeadline(time.Now().Add(200 * time.Millisecond))
		if _, err := fmt.Fprintln(conn, out); err != nil {
			conn.Close()
			delete(t.subscribers, conn)
		}
	}
}

func (t *Timer) Tick() {
	t.mu.Lock()
	defer t.mu.Unlock()

	if t.running {
		t.remaining--
		if t.remaining <= 0 {
			if t.mode == "work" {
				_ = stats.LogSession("work", t.workDuration)
				t.completedCycles++
				if t.completedCycles >= t.totalCycles {
					t.mode = "long_break"
					t.remaining = t.longBreakDuration
					t.running = true
					go func() {
						triggerToast("All Pomodoros Complete! 🎉", fmt.Sprintf("Great job completing %d focus sessions! Starting %d-minute long break.", t.totalCycles, t.longBreakDuration/60))
						playNotificationSound("complete")
					}()
				} else {
					t.mode = "break"
					t.remaining = t.breakDuration
					t.running = true
					go func() {
						triggerToast("Pomodoro Complete! 🍅", fmt.Sprintf("Great focus! Starting %d-minute break.", t.breakDuration/60))
						playNotificationSound("complete")
					}()
				}
			} else if t.mode == "break" {
				t.mode = "work"
				t.remaining = t.workDuration
				t.running = true
				go func() {
					triggerToast("Break Ended! ☕", "Starting next focus session!")
					playNotificationSound("message-new-instant")
				}()
			} else if t.mode == "long_break" {
				t.completedCycles = 0
				t.mode = "work"
				t.remaining = t.workDuration
				t.running = true
				go func() {
					triggerToast("Long Break Ended! ☕", "Resetting cycles. Starting next focus session!")
					playNotificationSound("message-new-instant")
				}()
			}
		}
		t.broadcastLocked()
	}
}

func (t *Timer) handleConnection(conn net.Conn) {
	scanner := bufio.NewScanner(conn)
	if !scanner.Scan() {
		conn.Close()
		return
	}

	cmd := strings.TrimSpace(scanner.Text())
	switch {
	case cmd == "subscribe":
		t.mu.Lock()
		t.subscribers[conn] = struct{}{}
		out := waybar.Format(t.mode, t.remaining, t.totalLocked(), t.running)
		t.mu.Unlock()

		_ = conn.SetWriteDeadline(time.Now().Add(500 * time.Millisecond))
		_, _ = fmt.Fprintln(conn, out)
		_ = conn.SetWriteDeadline(time.Time{})

		// Keep connection open until client disconnects
		for scanner.Scan() {
		}

		t.mu.Lock()
		delete(t.subscribers, conn)
		t.mu.Unlock()
		conn.Close()

	case cmd == "toggle":
		t.mu.Lock()
		t.running = !t.running
		t.broadcastLocked()
		t.mu.Unlock()

		_, _ = fmt.Fprintln(conn, "OK")
		conn.Close()

	case cmd == "stop":
		t.mu.Lock()
		t.running = false
		t.mode = "work"
		t.remaining = t.workDuration
		t.broadcastLocked()
		t.mu.Unlock()

		_, _ = fmt.Fprintln(conn, "OK")
		conn.Close()

	case cmd == "reset":
		t.mu.Lock()
		t.running = false
		if t.mode == "break" {
			t.remaining = t.breakDuration
		} else {
			t.remaining = t.workDuration
		}
		t.broadcastLocked()
		t.mu.Unlock()

		_, _ = fmt.Fprintln(conn, "OK")
		conn.Close()

	case cmd == "skip":
		t.mu.Lock()
		t.running = false
		if t.mode == "work" {
			t.mode = "break"
			t.remaining = t.breakDuration
		} else {
			t.mode = "work"
			t.remaining = t.workDuration
		}
		t.broadcastLocked()
		t.mu.Unlock()

		_, _ = fmt.Fprintln(conn, "OK")
		conn.Close()

	case cmd == "status":
		t.mu.Lock()
		out := waybar.Format(t.mode, t.remaining, t.totalLocked(), t.running)
		t.mu.Unlock()

		_, _ = fmt.Fprintln(conn, out)
		conn.Close()

	case strings.HasPrefix(cmd, "set_settings "):
		var w, b, lb, tc int
		if _, err := fmt.Sscanf(cmd, "set_settings %d %d %d %d", &w, &b, &lb, &tc); err == nil && w > 0 && b > 0 && lb > 0 && tc > 0 {
			t.mu.Lock()
			t.workDuration = w * 60
			t.breakDuration = b * 60
			t.longBreakDuration = lb * 60
			t.totalCycles = tc
			_ = config.Save(config.Config{
				WorkDurationMinutes:      w,
				BreakDurationMinutes:     b,
				LongBreakDurationMinutes: lb,
				TotalCycles:              tc,
			})
			if !t.running {
				if t.mode == "work" {
					t.remaining = t.workDuration
				} else if t.mode == "break" {
					t.remaining = t.breakDuration
				} else if t.mode == "long_break" {
					t.remaining = t.longBreakDuration
				}
			}
			t.broadcastLocked()
			t.mu.Unlock()
		}
		_, _ = fmt.Fprintln(conn, "OK")
		conn.Close()

	case strings.HasPrefix(cmd, "set_durations "):
		var w, b int
		if _, err := fmt.Sscanf(cmd, "set_durations %d %d", &w, &b); err == nil && w > 0 && b > 0 {
			t.mu.Lock()
			t.workDuration = w * 60
			t.breakDuration = b * 60
			_ = config.Save(config.Config{
				WorkDurationMinutes:      w,
				BreakDurationMinutes:     b,
				LongBreakDurationMinutes: t.longBreakDuration / 60,
				TotalCycles:              t.totalCycles,
			})
			if !t.running {
				if t.mode == "work" {
					t.remaining = t.workDuration
				} else if t.mode == "break" {
					t.remaining = t.breakDuration
				}
			}
			t.broadcastLocked()
			t.mu.Unlock()
		}
		_, _ = fmt.Fprintln(conn, "OK")
		conn.Close()

	case strings.HasPrefix(cmd, "delete_block "):
		var idx int
		if _, err := fmt.Sscanf(cmd, "delete_block %d", &idx); err == nil && idx >= 0 {
			_ = stats.DeleteTodayBlock(idx)
			t.mu.Lock()
			t.broadcastLocked()
			t.mu.Unlock()
		}
		_, _ = fmt.Fprintln(conn, "OK")
		conn.Close()

	default:
		conn.Close()
	}
}

func triggerToast(title, message string) {
	exe, err := os.Executable()
	if err != nil {
		exe = "waybar-pomodoro"
	}
	_ = exec.Command(exe, "toast", title, message).Run()
}

func playNotificationSound(soundName string) {
	if err := exec.Command("canberra-gtk-play", "-i", soundName).Run(); err == nil {
		return
	}

	soundFile := fmt.Sprintf("/usr/share/sounds/freedesktop/stereo/%s.oga", soundName)
	if _, err := os.Stat(soundFile); err != nil {
		soundFile = "/usr/share/sounds/freedesktop/stereo/complete.oga"
	}

	for _, player := range []string{"pw-play", "paplay", "aplay"} {
		if exec.Command(player, soundFile).Run() == nil {
			return
		}
	}
}

func RunDaemon() {
	lockPath := ipc.GetLockPath() + ".daemon"
	lockFile, err := os.OpenFile(lockPath, os.O_CREATE|os.O_RDWR, 0o600)
	if err != nil {
		log.Fatalf("Failed to open daemon lock: %v", err)
	}
	defer lockFile.Close()

	if err := syscall.Flock(int(lockFile.Fd()), syscall.LOCK_EX|syscall.LOCK_NB); err != nil {
		// Daemon is already running
		return
	}

	sockPath := ipc.GetSocketPath()
	_ = os.Remove(sockPath)

	listener, err := net.Listen("unix", sockPath)
	if err != nil {
		log.Fatalf("Failed to bind socket: %v", err)
	}
	defer listener.Close()
	defer os.Remove(sockPath)

	sigChan := make(chan os.Signal, 1)
	signal.Notify(sigChan, os.Interrupt, syscall.SIGTERM)
	go func() {
		<-sigChan
		listener.Close()
		_ = os.Remove(sockPath)
		os.Exit(0)
	}()

	tm := New()

	go func() {
		for {
			conn, err := listener.Accept()
			if err != nil {
				return
			}
			go tm.handleConnection(conn)
		}
	}()

	ticker := time.NewTicker(1 * time.Second)
	defer ticker.Stop()

	for range ticker.C {
		tm.Tick()
	}
}

func RunClient() {
	for {
		if err := ipc.EnsureDaemonRunning(); err != nil {
			time.Sleep(1 * time.Second)
			continue
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

		scanner := bufio.NewScanner(conn)
		for scanner.Scan() {
			fmt.Println(scanner.Text())
		}

		conn.Close()
		time.Sleep(500 * time.Millisecond)
	}
}
