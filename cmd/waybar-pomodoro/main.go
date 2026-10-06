package main

import (
	"fmt"
	"os"
	"strings"

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
	case "ui", "gtk-ui":
		if err := ui.RunUI(); err != nil {
			fmt.Println("Error:", err)
			os.Exit(1)
		}
	case "toggle", "stop", "reset", "skip":
		if err := ipc.SendCommand(cmd); err != nil {
			fmt.Println("Error:", err)
			os.Exit(1)
		}
	case "delete_block", "delete-block":
		fullCmd := cmd
		if len(os.Args) > 2 {
			fullCmd = fmt.Sprintf("delete_block %s", os.Args[2])
		}
		if err := ipc.SendCommand(fullCmd); err != nil {
			fmt.Println("Error:", err)
			os.Exit(1)
		}
	case "set_block_project", "set-block-project":
		if len(os.Args) > 2 {
			fullCmd := fmt.Sprintf("set_block_project %s", strings.Join(os.Args[2:], " "))
			if err := ipc.SendCommand(fullCmd); err != nil {
				fmt.Println("Error:", err)
				os.Exit(1)
			}
		}
	case "add_session", "add-session":
		if len(os.Args) > 2 {
			fullCmd := fmt.Sprintf("add_session %s", strings.Join(os.Args[2:], " "))
			if err := ipc.SendCommand(fullCmd); err != nil {
				fmt.Println("Error:", err)
				os.Exit(1)
			}
		}
	case "edit_block", "edit-block", "edit_session", "edit-session":
		if len(os.Args) > 2 {
			fullCmd := fmt.Sprintf("edit_block %s", strings.Join(os.Args[2:], " "))
			if err := ipc.SendCommand(fullCmd); err != nil {
				fmt.Println("Error:", err)
				os.Exit(1)
			}
		}
	case "stats":
		if err := stats.ShowStats(); err != nil {
			fmt.Println("Error showing stats:", err)
			os.Exit(1)
		}
	case "dev":
		if err := ui.RunDevUI(); err != nil {
			fmt.Println("Error:", err)
			os.Exit(1)
		}
	case "toast":
		title := "Pomodoro Alert"
		msg := ""
		if len(os.Args) >= 3 {
			title = os.Args[2]
		}
		if len(os.Args) >= 4 {
			msg = os.Args[3]
		}
		if err := ui.RunToast(title, msg); err != nil {
			fmt.Println("Error showing toast:", err)
			os.Exit(1)
		}
	default:
		printUsage()
	}
}

func printUsage() {
	fmt.Println("Usage: waybar-pomodoro <waybar|ui|dev|toggle|stop|reset|skip|stats|daemon>")
}
