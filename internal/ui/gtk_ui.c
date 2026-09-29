#include <gtk/gtk.h>
#include <stdlib.h>
#include <string.h>

#include "gtk_ui.h"

static GtkApplication *application;
static GtkLabel *mode_label;
static GtkLabel *timer_label;
static GtkLabel *status_label;
static GtkLabel *cycle_label;
static GtkLabel *stats_label;
static GtkLabel *history_label;
static GtkProgressBar *progress_bar;
static int is_dev_mode = 0;

typedef struct {
	char *mode;
	char *timer;
	char *status;
	char *progress;
	char *cycle;
	char *stats;
	char *history;
	double fraction;
} UIUpdate;

static void set_label(GtkLabel *label, const char *value) {
	if (label != NULL) {
		gtk_label_set_text(label, value != NULL ? value : "");
	}
}

static gboolean apply_update(gpointer data) {
	UIUpdate *update = (UIUpdate *)data;
	set_label(mode_label, update->mode);
	set_label(timer_label, update->timer);
	set_label(status_label, update->status);
	set_label(cycle_label, update->cycle);
	set_label(stats_label, update->stats);
	set_label(history_label, update->history);
	if (progress_bar != NULL) {
		gtk_progress_bar_set_fraction(progress_bar, update->fraction);
	}
	if (mode_label != NULL) {
		gtk_widget_remove_css_class(GTK_WIDGET(mode_label), "break-mode");
		gtk_widget_remove_css_class(GTK_WIDGET(mode_label), "work-mode");
		gtk_widget_add_css_class(GTK_WIDGET(mode_label),
		                         strcmp(update->mode, "BREAK") == 0 ? "break-mode" : "work-mode");
	}
	g_free(update->mode);
	g_free(update->timer);
	g_free(update->status);
	g_free(update->progress);
	g_free(update->cycle);
	g_free(update->stats);
	g_free(update->history);
	g_free(update);
	return G_SOURCE_REMOVE;
}

void pom_gtk_update(const char *mode, const char *timer, const char *status,
                    const char *progress, const char *cycle,
                    const char *stats, const char *history, double fraction) {
	UIUpdate *update = g_new0(UIUpdate, 1);
	update->mode = g_strdup(mode);
	update->timer = g_strdup(timer);
	update->status = g_strdup(status);
	update->progress = g_strdup(progress);
	update->cycle = g_strdup(cycle);
	update->stats = g_strdup(stats);
	update->history = g_strdup(history);
	update->fraction = fraction;
	g_idle_add(apply_update, update);
}

static GtkWidget *make_label(const char *text, const char *css_class) {
	GtkWidget *label = gtk_label_new(text);
	gtk_widget_add_css_class(label, css_class);
	gtk_label_set_xalign(GTK_LABEL(label), 0.5f);
	return label;
}

static void button_clicked(GtkButton *button, gpointer command) {
	(void)button;
	if (is_dev_mode) {
		goGTKDevAction((char *)command);
	} else {
		goGTKCommand((char *)command);
	}
}

static GtkWidget *make_button(const char *icon, const char *command, const char *css_class) {
	GtkWidget *button = gtk_button_new_from_icon_name(icon);
	gtk_widget_add_css_class(button, css_class);
	gtk_widget_set_tooltip_text(button, command);
	gtk_widget_set_hexpand(button, TRUE);
	gtk_widget_set_size_request(button, -1, 52);
	g_signal_connect(button, "clicked", G_CALLBACK(button_clicked), (gpointer)command);
	return button;
}

static GtkWidget *build_pomodoro_card(void) {
	GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 7);
	gtk_widget_add_css_class(box, "popup-content");
	gtk_widget_set_size_request(box, 320, 360);

	mode_label = GTK_LABEL(make_label("FOCUS", "mode-label"));
	timer_label = GTK_LABEL(make_label("25:00", "timer-label"));
	status_label = GTK_LABEL(make_label("Paused", "muted-label"));
	gtk_box_append(GTK_BOX(box), GTK_WIDGET(mode_label));
	gtk_box_append(GTK_BOX(box), GTK_WIDGET(timer_label));
	gtk_box_append(GTK_BOX(box), GTK_WIDGET(status_label));

	progress_bar = GTK_PROGRESS_BAR(gtk_progress_bar_new());
	gtk_widget_add_css_class(GTK_WIDGET(progress_bar), "timer-progress");
	gtk_box_append(GTK_BOX(box), GTK_WIDGET(progress_bar));
	cycle_label = GTK_LABEL(make_label("●  ○  ○  ○    Cycle 1", "muted-label"));
	gtk_box_append(GTK_BOX(box), GTK_WIDGET(cycle_label));

	GtkWidget *separator = gtk_separator_new(GTK_ORIENTATION_HORIZONTAL);
	gtk_box_append(GTK_BOX(box), separator);
	stats_label = GTK_LABEL(make_label("0 sessions · 0 min focus", "stats-label"));
	history_label = GTK_LABEL(make_label("No sessions yet today", "muted-label"));
	gtk_box_append(GTK_BOX(box), GTK_WIDGET(stats_label));
	gtk_box_append(GTK_BOX(box), GTK_WIDGET(history_label));
	gtk_box_append(GTK_BOX(box), gtk_separator_new(GTK_ORIENTATION_HORIZONTAL));

	GtkWidget *buttons = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
	gtk_widget_set_margin_top(buttons, 8);
	gtk_box_set_homogeneous(GTK_BOX(buttons), TRUE);
	gtk_box_append(GTK_BOX(buttons), make_button("media-playback-start-symbolic", "toggle", "primary-button"));
	gtk_box_append(GTK_BOX(buttons), make_button("media-skip-forward-symbolic", "skip", "action-button"));
	gtk_box_append(GTK_BOX(buttons), make_button("view-refresh-symbolic", "reset", "action-button"));
	gtk_box_append(GTK_BOX(buttons), make_button("media-playback-stop-symbolic", "stop", "stop-button"));
	gtk_box_append(GTK_BOX(box), buttons);

	return box;
}

static gboolean key_pressed(GtkEventControllerKey *controller, guint keyval,
                            guint keycode, GdkModifierType state, gpointer data) {
	(void)controller;
	(void)keycode;
	(void)state;
	(void)data;
	switch (keyval) {
	case GDK_KEY_space: goGTKCommand("toggle"); return TRUE;
	case GDK_KEY_Escape:
	case GDK_KEY_q:
	case GDK_KEY_Q:
		pom_gtk_quit_async();
		return TRUE;
	case GDK_KEY_s: case GDK_KEY_S: goGTKCommand("skip"); return TRUE;
	case GDK_KEY_r: case GDK_KEY_R: goGTKCommand("reset"); return TRUE;
	case GDK_KEY_x: case GDK_KEY_X: goGTKCommand("stop"); return TRUE;
	default: return FALSE;
	}
}

static gboolean key_pressed_dev(GtkEventControllerKey *controller, guint keyval,
                                guint keycode, GdkModifierType state, gpointer data) {
	(void)controller;
	(void)keycode;
	(void)state;
	(void)data;
	switch (keyval) {
	case GDK_KEY_space: goGTKDevAction("toggle"); return TRUE;
	case GDK_KEY_m: case GDK_KEY_M: goGTKDevAction("mode"); return TRUE;
	case GDK_KEY_p: case GDK_KEY_P: goGTKDevAction("progress"); return TRUE;
	case GDK_KEY_c: case GDK_KEY_C: goGTKDevAction("cycle"); return TRUE;
	case GDK_KEY_plus: case GDK_KEY_equal: goGTKDevAction("add_minute"); return TRUE;
	case GDK_KEY_minus: goGTKDevAction("sub_minute"); return TRUE;
	case GDK_KEY_s: case GDK_KEY_S: goGTKDevAction("skip"); return TRUE;
	case GDK_KEY_r: case GDK_KEY_R: goGTKDevAction("reset"); return TRUE;
	case GDK_KEY_x: case GDK_KEY_X: goGTKDevAction("stop"); return TRUE;
	case GDK_KEY_Escape:
	case GDK_KEY_q:
	case GDK_KEY_Q:
		pom_gtk_quit_async();
		return TRUE;
	default: return FALSE;
	}
}

static void activate(GtkApplication *app, gpointer data) {
	(void)data;
	GtkWidget *window = gtk_application_window_new(app);
	gtk_window_set_title(GTK_WINDOW(window), "Pomodoro");
	gtk_window_set_default_size(GTK_WINDOW(window), 320, 360);
	gtk_window_set_resizable(GTK_WINDOW(window), FALSE);
	gtk_window_set_decorated(GTK_WINDOW(window), FALSE);
	gtk_widget_add_css_class(window, "pomodoro-window");

	gtk_window_set_child(GTK_WINDOW(window), build_pomodoro_card());

	GtkEventController *keys = gtk_event_controller_key_new();
	g_signal_connect(keys, "key-pressed", G_CALLBACK(key_pressed), NULL);
	gtk_widget_add_controller(window, keys);
	gtk_window_present(GTK_WINDOW(window));
}

static void activate_dev(GtkApplication *app, gpointer data) {
	(void)data;
	GtkWidget *window = gtk_application_window_new(app);
	gtk_window_set_title(GTK_WINDOW(window), "Pomodoro [DEV PREVIEW]");
	gtk_window_set_default_size(GTK_WINDOW(window), 540, 520);
	gtk_window_set_resizable(GTK_WINDOW(window), TRUE);
	gtk_window_set_decorated(GTK_WINDOW(window), TRUE);
	gtk_widget_add_css_class(window, "pomodoro-dev-window");

	GtkWidget *main_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
	gtk_window_set_child(GTK_WINDOW(window), main_box);

	GtkWidget *top_banner = make_label("DEV PREVIEW  •  320×360 Floating Window Simulation", "dev-banner");
	gtk_widget_set_margin_top(top_banner, 16);
	gtk_widget_set_margin_bottom(top_banner, 12);
	gtk_box_append(GTK_BOX(main_box), top_banner);

	GtkWidget *center_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
	gtk_widget_set_vexpand(center_box, TRUE);
	gtk_widget_set_hexpand(center_box, TRUE);
	gtk_widget_set_valign(center_box, GTK_ALIGN_CENTER);
	gtk_widget_set_halign(center_box, GTK_ALIGN_CENTER);

	GtkWidget *card_frame = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
	gtk_widget_add_css_class(card_frame, "card-preview");
	gtk_box_append(GTK_BOX(card_frame), build_pomodoro_card());
	gtk_box_append(GTK_BOX(center_box), card_frame);
	gtk_box_append(GTK_BOX(main_box), center_box);

	GtkWidget *bottom_banner = make_label("[M] Mode  •  [P] Progress  •  [C] Cycles  •  [+/-] Time  •  [Space] Play/Pause  •  [Q] Quit", "dev-banner");
	gtk_widget_set_margin_top(bottom_banner, 12);
	gtk_widget_set_margin_bottom(bottom_banner, 16);
	gtk_box_append(GTK_BOX(main_box), bottom_banner);

	GtkEventController *keys = gtk_event_controller_key_new();
	g_signal_connect(keys, "key-pressed", G_CALLBACK(key_pressed_dev), NULL);
	gtk_widget_add_controller(window, keys);
	gtk_window_present(GTK_WINDOW(window));
}

static void on_startup(GApplication *app, gpointer data) {
	(void)app;
	(void)data;
	static const char css[] =
		"window.pomodoro-window { background: #1e1e2e; color: #cdd6f4; }"
		"window.pomodoro-dev-window { background: #11111b; color: #cdd6f4; }"
		".card-preview { background: #1e1e2e; border-radius: 12px; border: 1.5px solid #45475a; }"
		".dev-banner { color: #a6adc8; font-size: 11px; font-weight: 500; }"
		".popup-content { padding: 18px; }"
		".mode-label { font-size: 12px; font-weight: bold; color: #fab387; }"
		".mode-label.break-mode { color: #89b4fa; }"
		".timer-label { font-family: monospace; font-size: 56px; font-weight: bold; color: #b4befe; }"
		".muted-label { color: #9399b2; }"
		".stats-label { color: #cdd6f4; font-size: 13px; }"
		"separator { background: #45475a; min-height: 1px; }"
		"progressbar.timer-progress trough { background: #313244; min-height: 6px; border-radius: 3px; }"
		"progressbar.timer-progress progress { background: #fab387; border-radius: 3px; }"
		"button { border: none; border-radius: 8px; min-height: 52px; background: #313244; color: #cdd6f4; }"
		"button.primary-button { background: #b4befe; color: #11111b; }"
		"button.stop-button { background: #f38ba8; color: #11111b; }";
	GtkCssProvider *provider = gtk_css_provider_new();
	gtk_css_provider_load_from_string(provider, css);
	GdkDisplay *display = gdk_display_get_default();
	if (display != NULL) {
		gtk_style_context_add_provider_for_display(display,
		                                           GTK_STYLE_PROVIDER(provider),
		                                           GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
	}
	g_object_unref(provider);
}

static gboolean quit_application(gpointer data) {
	if (application != NULL) {
		g_application_quit(G_APPLICATION(application));
	}
	return G_SOURCE_REMOVE;
}

void pom_gtk_quit_async(void) {
	g_idle_add(quit_application, NULL);
}

int pom_gtk_run(void) {
	is_dev_mode = 0;
	application = gtk_application_new("io.github.waybarpomodoro.gtk", G_APPLICATION_DEFAULT_FLAGS);
	g_signal_connect(application, "startup", G_CALLBACK(on_startup), NULL);
	g_signal_connect(application, "activate", G_CALLBACK(activate), NULL);
	int status = g_application_run(G_APPLICATION(application), 0, NULL);
	g_object_unref(application);
	application = NULL;
	return status;
}

int pom_gtk_dev_run(void) {
	is_dev_mode = 1;
	application = gtk_application_new("io.github.waybarpomodoro.gtkdev", G_APPLICATION_DEFAULT_FLAGS);
	g_signal_connect(application, "startup", G_CALLBACK(on_startup), NULL);
	g_signal_connect(application, "activate", G_CALLBACK(activate_dev), NULL);
	int status = g_application_run(G_APPLICATION(application), 0, NULL);
	g_object_unref(application);
	application = NULL;
	return status;
}
