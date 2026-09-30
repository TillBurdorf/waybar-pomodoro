.PHONY: build install restart dev clean

build:
	go build -o waybar-pomodoro ./cmd/waybar-pomodoro

install: build
	mkdir -p ~/.local/bin
	mv waybar-pomodoro ~/.local/bin/
	-pkill -f "waybar-pomodoro daemon"

restart:
	-pkill -f "waybar-pomodoro daemon"

dev:
	@chmod +x dev.sh
	@./dev.sh

clean:
	rm -f waybar-pomodoro

