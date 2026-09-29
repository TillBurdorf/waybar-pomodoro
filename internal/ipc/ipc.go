package ipc

import (
	"bufio"
	"fmt"
	"net"
	"os"
	"os/exec"
	"syscall"
	"time"
)

func GetSocketPath() string {
	return fmt.Sprintf("/tmp/waybar-pomodoro-%d.sock", os.Getuid())
}

func GetLockPath() string {
	return fmt.Sprintf("/tmp/waybar-pomodoro-%d.lock", os.Getuid())
}

func EnsureDaemonRunning() error {
	sockPath := GetSocketPath()

	// Fast path: check if daemon is already accepting connections
	conn, err := net.Dial("unix", sockPath)
	if err == nil {
		conn.Close()
		return nil
	}

	// Lock to avoid multiple instances attempting to spawn the daemon concurrently
	lockPath := GetLockPath()
	lockFile, err := os.OpenFile(lockPath, os.O_CREATE|os.O_RDWR, 0o600)
	if err != nil {
		return err
	}
	defer lockFile.Close()

	if err := syscall.Flock(int(lockFile.Fd()), syscall.LOCK_EX); err != nil {
		return err
	}
	defer syscall.Flock(int(lockFile.Fd()), syscall.LOCK_UN)

	// Re-check in case another process started the daemon while we were waiting for the lock
	conn, err = net.Dial("unix", sockPath)
	if err == nil {
		conn.Close()
		return nil
	}

	// Remove stale socket if it exists
	_ = os.Remove(sockPath)

	exe, err := os.Executable()
	if err != nil {
		return err
	}

	cmd := exec.Command(exe, "daemon")
	cmd.SysProcAttr = &syscall.SysProcAttr{Setsid: true}
	cmd.Stdout = nil
	cmd.Stderr = nil
	cmd.Stdin = nil

	if err := cmd.Start(); err != nil {
		return fmt.Errorf("failed to start daemon: %w", err)
	}

	// Wait up to 3 seconds for the daemon to start listening
	deadline := time.Now().Add(3 * time.Second)
	for time.Now().Before(deadline) {
		conn, err := net.Dial("unix", sockPath)
		if err == nil {
			conn.Close()
			return nil
		}
		time.Sleep(50 * time.Millisecond)
	}

	return fmt.Errorf("timed out waiting for pomodoro daemon to start")
}

func SendCommand(cmd string) error {
	if err := EnsureDaemonRunning(); err != nil {
		return err
	}

	conn, err := net.Dial("unix", GetSocketPath())
	if err != nil {
		return fmt.Errorf("failed to connect to daemon: %w", err)
	}
	defer conn.Close()

	if _, err := fmt.Fprintln(conn, cmd); err != nil {
		return err
	}

	scanner := bufio.NewScanner(conn)
	if scanner.Scan() {
		_ = scanner.Text()
	}
	return nil
}
