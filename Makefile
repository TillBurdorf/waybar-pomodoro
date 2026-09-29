.PHONY: build install clean

build:
	go build -o waybar-pomodoro ./cmd/waybar-pomodoro

install: build
	mkdir -p ~/.local/bin
	mv waybar-pomodoro ~/.local/bin/

clean:
	rm -f waybar-pomodoro
