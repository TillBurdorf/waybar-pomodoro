.PHONY: build install dev clean

build:
	go build -o waybar-pomodoro ./cmd/waybar-pomodoro

install: build
	mkdir -p ~/.local/bin
	mv waybar-pomodoro ~/.local/bin/

dev:
	@chmod +x dev.sh
	@./dev.sh

clean:
	rm -f waybar-pomodoro
