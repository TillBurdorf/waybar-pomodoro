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

	"waybar-pomodoro/internal/ipc"
	"waybar-pomodoro/internal/stats"
	"waybar-pomodoro/internal/waybar"
)

const (
	WorkDuration  = 25 * 60 // 25 minutes
	BreakDuration = 5 * 60  // 5 minutes
)

type Timer struct {
	mu          sync.Mutex
	mode        string // "work" or "break"
	remaining   int    // seconds
	running     bool
	subscribers map[net.Conn]struct{}
}

func New() *Timer {
	return &Timer{
		mode:        "work",
		remaining:   WorkDuration,
		running:     false,
		subscribers: make(map[net.Conn]struct{}),
	}
}

func (t *Timer) totalLocked() int {
	if t.mode == "break" {
		return BreakDuration
	}
	return WorkDuration
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
				_ = stats.LogSession("work", WorkDuration)
				t.mode = "break"
				t.remaining = BreakDuration
				go sendNotification("Pomodoro Complete! 🍅", "Great focus! Take a 5-minute break.")
			} else {
				t.mode = "work"
				t.remaining = WorkDuration
				go sendNotification("Break Ended! ☕", "Ready to focus again? Let's start!")
			}
			t.running = false
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
	switch cmd {
	case "subscribe":
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

	case "toggle":
		t.mu.Lock()
		t.running = !t.running
		t.broadcastLocked()
		t.mu.Unlock()

		_, _ = fmt.Fprintln(conn, "OK")
		conn.Close()

	case "stop":
		t.mu.Lock()
		t.running = false
		t.mode = "work"
		t.remaining = WorkDuration
		t.broadcastLocked()
		t.mu.Unlock()

		_, _ = fmt.Fprintln(conn, "OK")
		conn.Close()

	case "reset":
		t.mu.Lock()
		t.running = false
		if t.mode == "break" {
			t.remaining = BreakDuration
		} else {
			t.remaining = WorkDuration
		}
		t.broadcastLocked()
		t.mu.Unlock()

		_, _ = fmt.Fprintln(conn, "OK")
		conn.Close()

	case "skip":
		t.mu.Lock()
		t.running = false
		if t.mode == "work" {
			t.mode = "break"
			t.remaining = BreakDuration
		} else {
			t.mode = "work"
			t.remaining = WorkDuration
		}
		t.broadcastLocked()
		t.mu.Unlock()

		_, _ = fmt.Fprintln(conn, "OK")
		conn.Close()

	case "status":
		t.mu.Lock()
		out := waybar.Format(t.mode, t.remaining, t.totalLocked(), t.running)
		t.mu.Unlock()

		_, _ = fmt.Fprintln(conn, out)
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
