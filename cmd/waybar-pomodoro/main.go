package main

import (
	"fmt"
	"os"

	"waybar-pomodoro/internal/ipc"
	"waybar-pomodoro/internal/stats"
	"waybar-pomodoro/internal/timer"
	"waybar-pomodoro/internal/ui"
)

func main() {
	if len(os.Args) < 2 {
		printUsage()
		return
	}

	cmd := os.Args[1]

	switch cmd {
	case "daemon":
		timer.RunDaemon()
	case "waybar":
		timer.RunClient()
	case "ui":
		if err := ui.RunUI(); err != nil {
			fmt.Println("Error:", err)
			os.Exit(1)
		}
	case "toggle", "stop", "reset", "skip":
		if err := ipc.SendCommand(cmd); err != nil {
			fmt.Println("Error:", err)
			os.Exit(1)
		}
	case "stats":
		if err := stats.ShowStats(); err != nil {
			fmt.Println("Error showing stats:", err)
			os.Exit(1)
		}
	default:
		printUsage()
	}
}

func printUsage() {
	fmt.Println("Usage: waybar-pomodoro <waybar|ui|toggle|stop|reset|skip|stats|daemon>")
}
