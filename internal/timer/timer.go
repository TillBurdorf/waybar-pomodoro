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
	mu            sync.Mutex
	mode          string // "work" or "break"
	remaining     int    // seconds
	workDuration  int    // seconds
	breakDuration int    // seconds
	running       bool
	subscribers   map[net.Conn]struct{}
}

func New() *Timer {
	cfg := config.Load()
	w := cfg.WorkDurationMinutes * 60
	b := cfg.BreakDurationMinutes * 60
	return &Timer{
		mode:          "work",
		remaining:     w,
		workDuration:  w,
		breakDuration: b,
		running:       false,
		subscribers:   make(map[net.Conn]struct{}),
	}
}

func (t *Timer) totalLocked() int {
	if t.mode == "break" {
		return t.breakDuration
	}
	return t.workDuration
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
				t.mode = "break"
				t.remaining = t.breakDuration
				t.running = true
				go sendNotification("Pomodoro Complete! 🍅", fmt.Sprintf("Great focus! Starting %d-minute break.", t.breakDuration/60))
			} else {
				t.mode = "work"
				t.remaining = t.workDuration
				t.running = false
				go sendNotification("Break Ended! ☕", "Ready to focus again? Let's start!")
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

	case strings.HasPrefix(cmd, "set_durations "):
		var w, b int
		if _, err := fmt.Sscanf(cmd, "set_durations %d %d", &w, &b); err == nil && w > 0 && b > 0 {
			t.mu.Lock()
			t.workDuration = w * 60
			t.breakDuration = b * 60
			_ = config.Save(config.Config{WorkDurationMinutes: w, BreakDurationMinutes: b})
			if !t.running {
				if t.mode == "work" {
					t.remaining = t.workDuration
				} else {
					t.remaining = t.breakDuration
				}
			}
			t.broadcastLocked()
			t.mu.Unlock()
		}
		_, _ = fmt.Fprintln(conn, "OK")
		conn.Close()

	default:
		conn.Close()
	}
}

func sendNotification(title, message string) {
	_ = exec.Command("notify-send", "-a", "Waybar Pomodoro", "-u", "normal", title, message).Run()
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
