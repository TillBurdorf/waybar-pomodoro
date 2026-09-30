#include <gtk/gtk.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <stdint.h>

#include "gtk_ui.h"

static GtkApplication *application;
static GtkLabel *mode_label;
static GtkLabel *timer_label;
static GtkLabel *status_label;
static GtkLabel *cycle_label;
static GtkWidget *progress_ring = NULL;
static GtkWidget *main_stack = NULL;
static GtkWidget *stats_sub_stack = NULL;
static GtkWidget *stats_today_summary_lbl = NULL;
static GtkWidget *stats_today_box = NULL;
static GtkWidget *stats_week_summary_lbl = NULL;
static GtkWidget *stats_week_box = NULL;
static GtkWidget *timer_buttons[4] = {NULL, NULL, NULL, NULL};
static const char *timer_button_commands[4] = {"toggle", "skip", "reset", "stop"};
static int focused_timer_button_idx = 0;
static GtkWidget *setting_entries[2] = {NULL, NULL};
static double current_progress_fraction = 0.0;
static int current_is_break = 0;
static int is_dev_mode = 0;

static int work_duration_val = 25;
static int break_duration_val = 5;

void pom_gtk_set_initial_durations(int work_min, int break_min) {
    if (work_min > 0) work_duration_val = work_min;
    if (break_min > 0) break_duration_val = break_min;
}

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

static GtkWidget *dev_error_label = NULL;
static GtkWidget *dev_error_box = NULL;

static gboolean show_error_idle(gpointer data) {
    char *msg = (char *)data;
    if (dev_error_label != NULL && dev_error_box != NULL) {
        char formatted[600];
        snprintf(formatted, sizeof(formatted), "⚠️ %s", msg ? msg : "Rendering Error");
        gtk_label_set_text(GTK_LABEL(dev_error_label), formatted);
        gtk_widget_set_visible(dev_error_box, TRUE);
    }
    g_free(msg);
    return G_SOURCE_REMOVE;
}

static void dev_show_error(const char *msg) {
    if (!is_dev_mode || msg == NULL) return;
    g_idle_add(show_error_idle, g_strdup(msg));
}

static GLogWriterOutput dev_log_writer(GLogLevelFlags log_level,
                                       const GLogField *fields,
                                       gsize n_fields,
                                       gpointer user_data) {
    if (log_level & (G_LOG_LEVEL_ERROR | G_LOG_LEVEL_CRITICAL | G_LOG_LEVEL_WARNING)) {
        const char *msg = NULL;
        for (gsize i = 0; i < n_fields; i++) {
            if (fields[i].key != NULL && strcmp(fields[i].key, "MESSAGE") == 0) {
                msg = (const char *)fields[i].value;
                break;
            }
        }
        if (msg != NULL) {
            dev_show_error(msg);
        }
    }
    return g_log_writer_standard_streams(log_level, fields, n_fields, user_data);
}

static void set_label(GtkLabel *label, const char *value) {
    if (label != NULL) {
        gtk_label_set_text(label, value != NULL ? value : "");
    }
}

static void draw_progress_ring(GtkDrawingArea *area, cairo_t *cr, int width, int height, gpointer data) {
    (void)area;
    (void)data;
    if (cr == NULL || width < 10 || height < 10) return;

    if (isnan(current_progress_fraction) || isinf(current_progress_fraction)) {
        current_progress_fraction = 0.0;
        dev_show_error("Invalid progress fraction value (NaN or Inf)");
    }

    cairo_status_t status = cairo_status(cr);
    if (status != CAIRO_STATUS_SUCCESS) {
        dev_show_error(cairo_status_to_string(status));
        return;
    }

    double cx = width / 2.0;
    double cy = height / 2.0;
    double line_width = 7.0;
    double radius = (width < height ? width : height) / 2.0 - line_width / 2.0 - 4.0;
    if (radius < 10.0) return;

    cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);
    cairo_set_line_width(cr, line_width);

    // Track circle: Catppuccin Surface0 (#313244)
    cairo_arc(cr, cx, cy, radius, 0, 2.0 * M_PI);
    cairo_set_source_rgba(cr, 49.0 / 255.0, 50.0 / 255.0, 68.0 / 255.0, 1.0);
    cairo_stroke(cr);

    // Progress arc
    double frac = current_progress_fraction;
    if (frac < 0.0) frac = 0.0;
    if (frac > 1.0) frac = 1.0;

    if (frac >= 0.999) {
        cairo_arc(cr, cx, cy, radius, 0, 2.0 * M_PI);
        if (current_is_break) {
            // Catppuccin Blue (#89b4fa)
            cairo_set_source_rgba(cr, 137.0 / 255.0, 180.0 / 255.0, 250.0 / 255.0, 1.0);
        } else {
            // Catppuccin Lavender (#b4befe)
            cairo_set_source_rgba(cr, 180.0 / 255.0, 190.0 / 255.0, 254.0 / 255.0, 1.0);
        }
        cairo_stroke(cr);
    } else if (frac > 0.002) {
        double start_angle = -M_PI / 2.0; // 12 o'clock
        double end_angle = start_angle + frac * (2.0 * M_PI);

        cairo_arc(cr, cx, cy, radius, start_angle, end_angle);
        if (current_is_break) {
            // Catppuccin Blue (#89b4fa)
            cairo_set_source_rgba(cr, 137.0 / 255.0, 180.0 / 255.0, 250.0 / 255.0, 1.0);
        } else {
            // Catppuccin Lavender (#b4befe)
            cairo_set_source_rgba(cr, 180.0 / 255.0, 190.0 / 255.0, 254.0 / 255.0, 1.0);
        }
        cairo_stroke(cr);
    }

    status = cairo_status(cr);
    if (status != CAIRO_STATUS_SUCCESS) {
        dev_show_error(cairo_status_to_string(status));
    }
}

static gboolean apply_update(gpointer data) {
    UIUpdate *update = (UIUpdate *)data;
    set_label(mode_label, update->mode);
    set_label(timer_label, update->timer);
    set_label(status_label, update->status);
    set_label(cycle_label, update->cycle);
    current_progress_fraction = update->fraction;
    current_is_break = (update->mode != NULL && strcmp(update->mode, "BREAK") == 0);
    if (progress_ring != NULL) {
        gtk_widget_queue_draw(progress_ring);
    }
    if (mode_label != NULL) {
        gtk_widget_remove_css_class(GTK_WIDGET(mode_label), "break-mode");
        gtk_widget_remove_css_class(GTK_WIDGET(mode_label), "work-mode");
        gtk_widget_add_css_class(GTK_WIDGET(mode_label),
                                 current_is_break ? "break-mode" : "work-mode");
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

typedef struct {
    char *today_summary;
    char *today_blocks;
    char *week_summary;
    char *week_days;
} StatsUpdate;

static gboolean apply_stats_update(gpointer data) {
    StatsUpdate *up = (StatsUpdate *)data;
    if (stats_today_summary_lbl != NULL && up->today_summary != NULL) {
        gtk_label_set_text(GTK_LABEL(stats_today_summary_lbl), up->today_summary);
    }
    if (stats_week_summary_lbl != NULL && up->week_summary != NULL) {
        gtk_label_set_text(GTK_LABEL(stats_week_summary_lbl), up->week_summary);
    }

    // Populate Today's blocks
    if (stats_today_box != NULL) {
        GtkWidget *c = gtk_widget_get_first_child(stats_today_box);
        while (c != NULL) {
            GtkWidget *next = gtk_widget_get_next_sibling(c);
            gtk_box_remove(GTK_BOX(stats_today_box), c);
            c = next;
        }

        if (up->today_blocks == NULL || strlen(up->today_blocks) == 0) {
            GtkWidget *empty_lbl = gtk_label_new("No work blocks recorded yet today");
            gtk_widget_add_css_class(empty_lbl, "muted-label");
            gtk_widget_set_margin_top(empty_lbl, 24);
            gtk_box_append(GTK_BOX(stats_today_box), empty_lbl);
        } else {
            char **lines = g_strsplit(up->today_blocks, "\n", -1);
            for (int i = 0; lines[i] != NULL; i++) {
                if (strlen(lines[i]) == 0) continue;
                char **parts = g_strsplit(lines[i], "|", 3);
                if (parts[0] != NULL && parts[1] != NULL) {
                    const char *start = parts[0];
                    const char *end = parts[1];
                    const char *dur = parts[2] ? parts[2] : "30m";

                    GtkWidget *row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
                    gtk_widget_add_css_class(row, "timeline-row");

                    GtkWidget *time_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 1);
                    gtk_widget_set_size_request(time_box, 48, -1);
                    gtk_widget_set_valign(time_box, GTK_ALIGN_CENTER);

                    GtkWidget *s_lbl = gtk_label_new(start);
                    gtk_widget_add_css_class(s_lbl, "timeline-time");
                    gtk_label_set_xalign(GTK_LABEL(s_lbl), 1.0f);

                    GtkWidget *e_lbl = gtk_label_new(end);
                    gtk_widget_add_css_class(e_lbl, "timeline-time-end");
                    gtk_label_set_xalign(GTK_LABEL(e_lbl), 1.0f);

                    gtk_box_append(GTK_BOX(time_box), s_lbl);
                    gtk_box_append(GTK_BOX(time_box), e_lbl);

                    GtkWidget *card = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);
                    gtk_widget_add_css_class(card, "timeline-card");
                    gtk_widget_set_hexpand(card, TRUE);

                    GtkWidget *title = gtk_label_new("Focus Block");
                    gtk_widget_add_css_class(title, "timeline-title");
                    gtk_label_set_xalign(GTK_LABEL(title), 0.0f);

                    char sub_buf[64];
                    snprintf(sub_buf, sizeof(sub_buf), "%s  •  %s - %s", dur, start, end);
                    GtkWidget *sub = gtk_label_new(sub_buf);
                    gtk_widget_add_css_class(sub, "timeline-subtitle");
                    gtk_label_set_xalign(GTK_LABEL(sub), 0.0f);

                    gtk_box_append(GTK_BOX(card), title);
                    gtk_box_append(GTK_BOX(card), sub);

                    gtk_box_append(GTK_BOX(row), time_box);
                    gtk_box_append(GTK_BOX(row), card);
                    gtk_box_append(GTK_BOX(stats_today_box), row);
                }
                g_strfreev(parts);
            }
            g_strfreev(lines);
        }
    }

    // Populate Weekly days
    if (stats_week_box != NULL) {
        GtkWidget *c = gtk_widget_get_first_child(stats_week_box);
        while (c != NULL) {
            GtkWidget *next = gtk_widget_get_next_sibling(c);
            gtk_box_remove(GTK_BOX(stats_week_box), c);
            c = next;
        }

        if (up->week_days != NULL && strlen(up->week_days) > 0) {
            char **lines = g_strsplit(up->week_days, "\n", -1);
            int max_min = 60;
            for (int i = 0; lines[i] != NULL; i++) {
                if (strlen(lines[i]) == 0) continue;
                char **parts = g_strsplit(lines[i], "|", 5);
                if (parts[0] && parts[1] && parts[2]) {
                    int m = atoi(parts[2]);
                    if (m > max_min) max_min = m;
                }
                g_strfreev(parts);
            }

            for (int i = 0; lines[i] != NULL; i++) {
                if (strlen(lines[i]) == 0) continue;
                char **parts = g_strsplit(lines[i], "|", 5);
                if (parts[0] && parts[1] && parts[2] && parts[3]) {
                    const char *day = parts[0];
                    int m = atoi(parts[2]);
                    const char *timestr = parts[3];
                    int is_today = parts[4] ? atoi(parts[4]) : 0;

                    GtkWidget *row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
                    gtk_widget_add_css_class(row, "week-row");
                    if (is_today) {
                        gtk_widget_add_css_class(row, "is-today");
                    }

                    GtkWidget *d_lbl = gtk_label_new(day);
                    gtk_widget_add_css_class(d_lbl, is_today ? "week-day-today" : "week-day");
                    gtk_widget_set_size_request(d_lbl, 32, -1);
                    gtk_label_set_xalign(GTK_LABEL(d_lbl), 0.0f);

                    GtkWidget *pb = gtk_progress_bar_new();
                    gtk_widget_add_css_class(pb, is_today ? "week-bar-today" : "week-bar");
                    gtk_widget_set_hexpand(pb, TRUE);
                    gtk_widget_set_valign(pb, GTK_ALIGN_CENTER);
                    double frac = (double)m / (double)max_min;
                    if (frac < 0.0) frac = 0.0;
                    if (frac > 1.0) frac = 1.0;
                    gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(pb), frac);

                    GtkWidget *t_lbl = gtk_label_new(timestr);
                    gtk_widget_add_css_class(t_lbl, is_today ? "week-time-today" : "week-time");
                    gtk_widget_set_size_request(t_lbl, 55, -1);
                    gtk_label_set_xalign(GTK_LABEL(t_lbl), 1.0f);

                    gtk_box_append(GTK_BOX(row), d_lbl);
                    gtk_box_append(GTK_BOX(row), pb);
                    gtk_box_append(GTK_BOX(row), t_lbl);
                    gtk_box_append(GTK_BOX(stats_week_box), row);
                }
                g_strfreev(parts);
            }
            g_strfreev(lines);
        }
    }

    g_free(up->today_summary);
    g_free(up->today_blocks);
    g_free(up->week_summary);
    g_free(up->week_days);
    g_free(up);
    return G_SOURCE_REMOVE;
}

void pom_gtk_update_stats(const char *today_summary, const char *today_blocks_data,
                          const char *week_summary, const char *week_days_data) {
    StatsUpdate *up = g_new0(StatsUpdate, 1);
    up->today_summary = g_strdup(today_summary);
    up->today_blocks = g_strdup(today_blocks_data);
    up->week_summary = g_strdup(week_summary);
    up->week_days = g_strdup(week_days_data);
    g_idle_add(apply_stats_update, up);
}

static GtkWidget *make_label(const char *text, const char *css_class) {
    GtkWidget *label = gtk_label_new(text);
    gtk_widget_add_css_class(label, css_class);
    gtk_label_set_xalign(GTK_LABEL(label), 0.5f);
    return label;
}

static void update_timer_button_focus(int new_idx) {
    if (new_idx < 0) new_idx = 3;
    if (new_idx > 3) new_idx = 0;
    focused_timer_button_idx = new_idx;
    for (int i = 0; i < 4; i++) {
        if (timer_buttons[i] != NULL) {
            GtkWidget *child = gtk_button_get_child(GTK_BUTTON(timer_buttons[i]));
            if (i == focused_timer_button_idx) {
                gtk_widget_add_css_class(timer_buttons[i], "focused");
                if (child != NULL && GTK_IS_IMAGE(child)) {
                    gtk_image_set_pixel_size(GTK_IMAGE(child), 22);
                }
            } else {
                gtk_widget_remove_css_class(timer_buttons[i], "focused");
                if (child != NULL && GTK_IS_IMAGE(child)) {
                    gtk_image_set_pixel_size(GTK_IMAGE(child), 16);
                }
            }
        }
    }
}

static void trigger_focused_timer_button(void) {
    if (focused_timer_button_idx < 0 || focused_timer_button_idx >= 4) return;
    const char *cmd = timer_button_commands[focused_timer_button_idx];
    if (is_dev_mode) {
        goGTKDevAction((char *)cmd);
    } else {
        goGTKCommand((char *)cmd);
    }
}

static void button_clicked(GtkButton *button, gpointer command) {
    for (int i = 0; i < 4; i++) {
        if (timer_buttons[i] == GTK_WIDGET(button)) {
            update_timer_button_focus(i);
            break;
        }
    }
    if (is_dev_mode) {
        goGTKDevAction((char *)command);
    } else {
        goGTKCommand((char *)command);
    }
}

static GtkWidget *make_button(const char *icon, const char *command, const char *css_class) {
    GtkWidget *button = gtk_button_new_from_icon_name(icon);
    GtkWidget *child = gtk_button_get_child(GTK_BUTTON(button));
    if (child != NULL && GTK_IS_IMAGE(child)) {
        gtk_image_set_pixel_size(GTK_IMAGE(child), 16);
    }
    gtk_widget_add_css_class(button, css_class);
    gtk_widget_set_tooltip_text(button, command);
    gtk_widget_set_size_request(button, 40, 40);
    g_signal_connect(button, "clicked", G_CALLBACK(button_clicked), (gpointer)command);
    return button;
}

/* Builds Tab 1: Timer Page */
static GtkWidget *build_timer_page(void) {
    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_widget_set_vexpand(box, TRUE);
    gtk_widget_set_hexpand(box, TRUE);

    GtkWidget *overlay = gtk_overlay_new();
    gtk_widget_set_halign(overlay, GTK_ALIGN_CENTER);
    gtk_widget_set_valign(overlay, GTK_ALIGN_CENTER);
    gtk_widget_set_vexpand(overlay, TRUE);

    GtkWidget *drawing_area = gtk_drawing_area_new();
    gtk_widget_set_size_request(drawing_area, 190, 190);
    gtk_drawing_area_set_draw_func(GTK_DRAWING_AREA(drawing_area), draw_progress_ring, NULL, NULL);
    progress_ring = drawing_area;
    gtk_overlay_set_child(GTK_OVERLAY(overlay), drawing_area);

    GtkWidget *inner_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);
    gtk_widget_set_halign(inner_box, GTK_ALIGN_CENTER);
    gtk_widget_set_valign(inner_box, GTK_ALIGN_CENTER);

    mode_label = GTK_LABEL(make_label("FOCUS", "mode-label"));
    timer_label = GTK_LABEL(make_label("25:00", "timer-label"));
    status_label = GTK_LABEL(make_label("Paused", "muted-label"));

    gtk_box_append(GTK_BOX(inner_box), GTK_WIDGET(mode_label));
    gtk_box_append(GTK_BOX(inner_box), GTK_WIDGET(timer_label));
    gtk_box_append(GTK_BOX(inner_box), GTK_WIDGET(status_label));

    gtk_overlay_add_overlay(GTK_OVERLAY(overlay), inner_box);

    cycle_label = GTK_LABEL(make_label("●  ○  ○  ○", "cycle-dots"));
    gtk_widget_set_vexpand(GTK_WIDGET(cycle_label), TRUE);
    gtk_widget_set_valign(GTK_WIDGET(cycle_label), GTK_ALIGN_CENTER);

    GtkWidget *buttons = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 18);
    gtk_widget_set_halign(buttons, GTK_ALIGN_CENTER);
    gtk_widget_set_vexpand(buttons, TRUE);
    gtk_widget_set_valign(buttons, GTK_ALIGN_CENTER);
    timer_buttons[0] = make_button("media-playback-start-symbolic", "toggle", "ctrl-btn");
    timer_buttons[1] = make_button("media-skip-forward-symbolic", "skip", "ctrl-btn");
    timer_buttons[2] = make_button("view-refresh-symbolic", "reset", "ctrl-btn");
    timer_buttons[3] = make_button("media-playback-stop-symbolic", "stop", "ctrl-btn");
    for (int i = 0; i < 4; i++) {
        gtk_box_append(GTK_BOX(buttons), timer_buttons[i]);
    }
    update_timer_button_focus(0);

    gtk_box_append(GTK_BOX(box), overlay);
    gtk_box_append(GTK_BOX(box), GTK_WIDGET(cycle_label));
    gtk_box_append(GTK_BOX(box), buttons);

    return box;
}

static GtkWidget *build_stats_today_page(void) {
    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);
    gtk_widget_set_vexpand(box, TRUE);
    gtk_widget_set_hexpand(box, TRUE);

    stats_today_summary_lbl = make_label("0 sessions · 0m focus", "stats-sub-header");
    gtk_widget_set_halign(stats_today_summary_lbl, GTK_ALIGN_CENTER);
    gtk_box_append(GTK_BOX(box), stats_today_summary_lbl);

    GtkWidget *scroll = gtk_scrolled_window_new();
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll), GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
    gtk_widget_set_vexpand(scroll, TRUE);
    gtk_widget_set_hexpand(scroll, TRUE);
    gtk_widget_set_size_request(scroll, -1, 210);

    stats_today_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);
    gtk_widget_set_vexpand(stats_today_box, TRUE);
    gtk_widget_set_hexpand(stats_today_box, TRUE);
    gtk_widget_add_css_class(stats_today_box, "timeline-box");

    GtkWidget *empty_lbl = gtk_label_new("No work blocks recorded yet today");
    gtk_widget_add_css_class(empty_lbl, "muted-label");
    gtk_widget_set_margin_top(empty_lbl, 24);
    gtk_box_append(GTK_BOX(stats_today_box), empty_lbl);

    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scroll), stats_today_box);
    gtk_box_append(GTK_BOX(box), scroll);

    return box;
}

static GtkWidget *build_stats_week_page(void) {
    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);
    gtk_widget_set_vexpand(box, TRUE);
    gtk_widget_set_hexpand(box, TRUE);

    stats_week_summary_lbl = make_label("This Week: 0m total", "stats-sub-header");
    gtk_widget_set_halign(stats_week_summary_lbl, GTK_ALIGN_CENTER);
    gtk_box_append(GTK_BOX(box), stats_week_summary_lbl);

    stats_week_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
    gtk_widget_set_vexpand(stats_week_box, TRUE);
    gtk_widget_set_hexpand(stats_week_box, TRUE);
    gtk_widget_set_margin_top(stats_week_box, 4);

    gtk_box_append(GTK_BOX(box), stats_week_box);

    return box;
}

/* Builds Tab 2: Statistics Page with Today and Weekly sub-tabs */
static GtkWidget *build_stats_page(void) {
    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    gtk_widget_set_vexpand(box, TRUE);
    gtk_widget_set_hexpand(box, TRUE);
    gtk_widget_set_valign(box, GTK_ALIGN_FILL);
    gtk_widget_set_margin_top(box, 4);

    stats_sub_stack = gtk_stack_new();
    gtk_stack_set_transition_type(GTK_STACK(stats_sub_stack), GTK_STACK_TRANSITION_TYPE_SLIDE_LEFT_RIGHT);
    gtk_widget_set_vexpand(stats_sub_stack, TRUE);
    gtk_widget_set_hexpand(stats_sub_stack, TRUE);

    GtkWidget *today_page = build_stats_today_page();
    GtkWidget *week_page = build_stats_week_page();

    GtkStackPage *p_today = gtk_stack_add_child(GTK_STACK(stats_sub_stack), today_page);
    gtk_stack_page_set_name(p_today, "today");
    gtk_stack_page_set_title(p_today, "Today");

    GtkStackPage *p_week = gtk_stack_add_child(GTK_STACK(stats_sub_stack), week_page);
    gtk_stack_page_set_name(p_week, "week");
    gtk_stack_page_set_title(p_week, "Weekly");

    GtkWidget *sub_switcher = gtk_stack_switcher_new();
    gtk_stack_switcher_set_stack(GTK_STACK_SWITCHER(sub_switcher), GTK_STACK(stats_sub_stack));
    gtk_widget_set_halign(sub_switcher, GTK_ALIGN_CENTER);
    gtk_widget_add_css_class(sub_switcher, "stats-sub-switcher");
    gtk_widget_set_margin_top(sub_switcher, 4);
    gtk_widget_set_margin_bottom(sub_switcher, 2);

    gtk_box_append(GTK_BOX(box), stats_sub_stack);
    gtk_box_append(GTK_BOX(box), sub_switcher);

    return box;
}

static void on_duration_entry_changed(GtkEditable *editable, gpointer user_data) {
    const char *text = gtk_editable_get_text(editable);
    if (text == NULL || strlen(text) == 0) return;
    int val = atoi(text);
    int type = (int)(intptr_t)user_data;
    if (type == 0) {
        if (val >= 1 && val <= 300) {
            work_duration_val = val;
            goGTKSetDurations(work_duration_val, break_duration_val);
        }
    } else {
        if (val >= 1 && val <= 180) {
            break_duration_val = val;
            goGTKSetDurations(work_duration_val, break_duration_val);
        }
    }
}

static gboolean select_all_idle(gpointer user_data) {
    if (GTK_IS_EDITABLE(user_data)) {
        gtk_editable_select_region(GTK_EDITABLE(user_data), 0, -1);
    }
    return G_SOURCE_REMOVE;
}

static void on_duration_entry_focus_enter(GtkEventControllerFocus *controller, gpointer user_data) {
    (void)user_data;
    GtkWidget *entry = gtk_event_controller_get_widget(GTK_EVENT_CONTROLLER(controller));
    g_idle_add(select_all_idle, entry);
}

static void on_duration_entry_click_released(GtkGestureClick *gesture, int n_press, double x, double y, gpointer user_data) {
    (void)gesture;
    (void)n_press;
    (void)x;
    (void)y;
    GtkWidget *entry = GTK_WIDGET(user_data);
    g_idle_add(select_all_idle, entry);
}

static void on_duration_entry_focus_leave(GtkEventControllerFocus *controller, gpointer user_data) {
    GtkWidget *entry = GTK_WIDGET(gtk_event_controller_get_widget(GTK_EVENT_CONTROLLER(controller)));
    int type = (int)(intptr_t)user_data;
    int current_val = (type == 0) ? work_duration_val : break_duration_val;
    const char *text = gtk_editable_get_text(GTK_EDITABLE(entry));
    int val = (text != NULL) ? atoi(text) : 0;
    if (val < 1 || (type == 0 && val > 300) || (type == 1 && val > 180)) {
        char buf[16];
        snprintf(buf, sizeof(buf), "%d", current_val);
        gtk_editable_set_text(GTK_EDITABLE(entry), buf);
    }
}

static void on_duration_entry_activate(GtkEntry *entry, gpointer user_data) {
    int type = (int)(intptr_t)user_data;
    int current_val = (type == 0) ? work_duration_val : break_duration_val;
    const char *text = gtk_editable_get_text(GTK_EDITABLE(entry));
    int val = (text != NULL) ? atoi(text) : 0;
    if (val >= 1 && ((type == 0 && val <= 300) || (type == 1 && val <= 180))) {
        if (type == 0) work_duration_val = val;
        else break_duration_val = val;
        goGTKSetDurations(work_duration_val, break_duration_val);
    } else {
        char buf[16];
        snprintf(buf, sizeof(buf), "%d", current_val);
        gtk_editable_set_text(GTK_EDITABLE(entry), buf);
    }
}

static GtkWidget *build_duration_setting_block(const char *title, int initial_val, int setting_type) {
    GtkWidget *block = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);
    gtk_widget_set_halign(block, GTK_ALIGN_CENTER);

    GtkWidget *t_lbl = make_label(title, "setting-title");
    gtk_widget_set_halign(t_lbl, GTK_ALIGN_CENTER);
    gtk_box_append(GTK_BOX(block), t_lbl);

    GtkWidget *input_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    gtk_widget_set_halign(input_box, GTK_ALIGN_CENTER);
    gtk_widget_set_valign(input_box, GTK_ALIGN_CENTER);

    GtkWidget *entry = gtk_entry_new();
    if (setting_type >= 0 && setting_type < 2) {
        setting_entries[setting_type] = entry;
    }
    gtk_widget_add_css_class(entry, "setting-entry");
    gtk_entry_set_max_length(GTK_ENTRY(entry), 3);
    gtk_editable_set_width_chars(GTK_EDITABLE(entry), 3);
    gtk_editable_set_max_width_chars(GTK_EDITABLE(entry), 3);
    gtk_entry_set_alignment(GTK_ENTRY(entry), 0.5f);
    gtk_widget_set_hexpand(entry, FALSE);
    gtk_widget_set_size_request(entry, 38, 26);

    char val_str[16];
    snprintf(val_str, sizeof(val_str), "%d", initial_val);
    gtk_editable_set_text(GTK_EDITABLE(entry), val_str);

    g_signal_connect(entry, "changed", G_CALLBACK(on_duration_entry_changed), (gpointer)(intptr_t)setting_type);
    g_signal_connect(entry, "activate", G_CALLBACK(on_duration_entry_activate), (gpointer)(intptr_t)setting_type);

    GtkEventController *focus_ctrl = gtk_event_controller_focus_new();
    g_signal_connect(focus_ctrl, "enter", G_CALLBACK(on_duration_entry_focus_enter), NULL);
    g_signal_connect(focus_ctrl, "leave", G_CALLBACK(on_duration_entry_focus_leave), (gpointer)(intptr_t)setting_type);
    gtk_widget_add_controller(entry, focus_ctrl);

    GtkGesture *click_gesture = gtk_gesture_click_new();
    gtk_gesture_single_set_button(GTK_GESTURE_SINGLE(click_gesture), 0);
    g_signal_connect(click_gesture, "released", G_CALLBACK(on_duration_entry_click_released), entry);
    gtk_widget_add_controller(entry, GTK_EVENT_CONTROLLER(click_gesture));

    GtkWidget *unit_lbl = make_label("min", "setting-unit");

    gtk_box_append(GTK_BOX(input_box), entry);
    gtk_box_append(GTK_BOX(input_box), unit_lbl);

    gtk_box_append(GTK_BOX(block), input_box);

    return block;
}

static void focus_setting_entry(int idx) {
    if (idx < 0) idx = 0;
    if (idx > 1) idx = 1;
    if (setting_entries[idx] != NULL) {
        gtk_widget_grab_focus(setting_entries[idx]);
        gtk_editable_select_region(GTK_EDITABLE(setting_entries[idx]), 0, -1);
    }
}

static void switch_setting_entry(int direction) {
    GtkRoot *root = main_stack ? gtk_widget_get_root(main_stack) : NULL;
    GtkWidget *focus = (root && GTK_IS_WINDOW(root)) ? gtk_window_get_focus(GTK_WINDOW(root)) : NULL;

    int current_idx = -1;
    for (int i = 0; i < 2; i++) {
        if (setting_entries[i] != NULL) {
            if (focus == setting_entries[i] || gtk_widget_is_ancestor(focus, setting_entries[i])) {
                current_idx = i;
                break;
            }
        }
    }

    int next_idx;
    if (current_idx == -1) {
        next_idx = (direction > 0) ? 1 : 0;
    } else {
        if (direction > 0) {
            next_idx = (current_idx + 1) % 2;
        } else {
            next_idx = (current_idx - 1 + 2) % 2;
        }
    }
    focus_setting_entry(next_idx);
}

/* Builds Tab 3: Settings Page */
static GtkWidget *build_settings_page(void) {
    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 14);
    gtk_widget_set_vexpand(box, TRUE);
    gtk_widget_set_hexpand(box, TRUE);
    gtk_widget_set_valign(box, GTK_ALIGN_START);
    gtk_widget_set_margin_top(box, 16);

    GtkWidget *title = make_label("Timer Settings", "stats-title");
    gtk_box_append(GTK_BOX(box), title);

    gtk_box_append(GTK_BOX(box), gtk_separator_new(GTK_ORIENTATION_HORIZONTAL));

    GtkWidget *work_block = build_duration_setting_block("Focus Duration", work_duration_val, 0);
    gtk_box_append(GTK_BOX(box), work_block);

    GtkWidget *break_block = build_duration_setting_block("Break Duration", break_duration_val, 1);
    gtk_box_append(GTK_BOX(box), break_block);

    return box;
}

static gboolean focus_first_entry_idle(gpointer user_data) {
    (void)user_data;
    if (main_stack != NULL) {
        const char *name = gtk_stack_get_visible_child_name(GTK_STACK(main_stack));
        if (name != NULL && strcmp(name, "settings") == 0) {
            focus_setting_entry(0);
        }
    }
    return G_SOURCE_REMOVE;
}

static void on_stack_visible_child_changed(GObject *gobject, GParamSpec *pspec, gpointer user_data) {
    (void)pspec;
    (void)user_data;
    const char *name = gtk_stack_get_visible_child_name(GTK_STACK(gobject));
    if (name != NULL && strcmp(name, "settings") == 0) {
        focus_setting_entry(0);
        g_idle_add(focus_first_entry_idle, NULL);
    } else {
        GtkRoot *root = gtk_widget_get_root(GTK_WIDGET(gobject));
        if (root != NULL && GTK_IS_WINDOW(root)) {
            gtk_window_set_focus(GTK_WINDOW(root), NULL);
        }
    }
}

/* Tabbed Popup Container */
static GtkWidget *build_pomodoro_card(void) {
    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 12);
    gtk_widget_add_css_class(box, "popup-content");
    gtk_widget_set_size_request(box, 320, 380);

    // Stack container for tabs
    GtkWidget *stack = gtk_stack_new();
    gtk_stack_set_transition_type(GTK_STACK(stack), GTK_STACK_TRANSITION_TYPE_SLIDE_LEFT_RIGHT);
    gtk_widget_set_vexpand(stack, TRUE);
    gtk_widget_set_hexpand(stack, TRUE);

    GtkWidget *timer_page = build_timer_page();
    GtkWidget *stats_page = build_stats_page();
    GtkWidget *settings_page = build_settings_page();

    GtkStackPage *page_timer = gtk_stack_add_child(GTK_STACK(stack), timer_page);
    gtk_stack_page_set_name(page_timer, "timer");
    gtk_stack_page_set_icon_name(page_timer, "preferences-system-time-symbolic");

    GtkStackPage *page_stats = gtk_stack_add_child(GTK_STACK(stack), stats_page);
    gtk_stack_page_set_name(page_stats, "stats");
    gtk_stack_page_set_icon_name(page_stats, "network-cellular-signal-excellent-symbolic");

    GtkStackPage *page_settings = gtk_stack_add_child(GTK_STACK(stack), settings_page);
    gtk_stack_page_set_name(page_settings, "settings");
    gtk_stack_page_set_icon_name(page_settings, "emblem-system-symbolic");

    // Tab switcher header
    GtkWidget *switcher = gtk_stack_switcher_new();
    gtk_stack_switcher_set_stack(GTK_STACK_SWITCHER(switcher), GTK_STACK(stack));
    gtk_stack_set_visible_child_name(GTK_STACK(stack), "timer");
    gtk_widget_set_halign(switcher, GTK_ALIGN_CENTER);
    gtk_widget_add_css_class(switcher, "tab-switcher");

    GtkWidget *tab_btn = gtk_widget_get_first_child(switcher);
    if (tab_btn != NULL) {
        gtk_widget_set_tooltip_text(tab_btn, "Timer");
        tab_btn = gtk_widget_get_next_sibling(tab_btn);
    }
    if (tab_btn != NULL) {
        gtk_widget_set_tooltip_text(tab_btn, "Stats");
        tab_btn = gtk_widget_get_next_sibling(tab_btn);
    }
    if (tab_btn != NULL) {
        gtk_widget_set_tooltip_text(tab_btn, "Settings");
    }

    main_stack = stack;
    g_signal_connect(stack, "notify::visible-child-name", G_CALLBACK(on_stack_visible_child_changed), NULL);

    gtk_box_append(GTK_BOX(box), switcher);
    gtk_box_append(GTK_BOX(box), stack);

    return box;
}

static void switch_tab(int forward) {
    if (main_stack == NULL) return;
    const char *current = gtk_stack_get_visible_child_name(GTK_STACK(main_stack));
    static const char *tabs[] = {"timer", "stats", "settings"};
    int count = sizeof(tabs) / sizeof(tabs[0]);
    int idx = 0;
    if (current != NULL) {
        for (int i = 0; i < count; i++) {
            if (strcmp(tabs[i], current) == 0) {
                idx = i;
                break;
            }
        }
    }
    if (forward) {
        idx = (idx + 1) % count;
    } else {
        idx = (idx - 1 + count) % count;
    }
    gtk_stack_set_visible_child_name(GTK_STACK(main_stack), tabs[idx]);

    if (strcmp(tabs[idx], "settings") == 0) {
        focus_setting_entry(0);
        g_idle_add(focus_first_entry_idle, NULL);
    } else {
        GtkRoot *root = gtk_widget_get_root(main_stack);
        if (root != NULL && GTK_IS_WINDOW(root)) {
            gtk_window_set_focus(GTK_WINDOW(root), NULL);
        }
    }
}

static void cycle_stats_sub_tab(int forward) {
    if (stats_sub_stack == NULL) return;
    const char *current = gtk_stack_get_visible_child_name(GTK_STACK(stats_sub_stack));
    static const char *sub_tabs[] = {"today", "week"};
    int count = sizeof(sub_tabs) / sizeof(sub_tabs[0]);
    int idx = 0;
    if (current != NULL) {
        for (int i = 0; i < count; i++) {
            if (strcmp(sub_tabs[i], current) == 0) {
                idx = i;
                break;
            }
        }
    }
    if (forward) {
        idx = (idx + 1) % count;
    } else {
        idx = (idx - 1 + count) % count;
    }
    gtk_stack_set_visible_child_name(GTK_STACK(stats_sub_stack), sub_tabs[idx]);
}

static gboolean key_pressed(GtkEventControllerKey *controller, guint keyval,
                            guint keycode, GdkModifierType state, gpointer data) {
    (void)keycode;
    (void)data;
    if (keyval == GDK_KEY_Tab || keyval == GDK_KEY_ISO_Left_Tab) {
        gboolean backward = (keyval == GDK_KEY_ISO_Left_Tab) || ((state & GDK_SHIFT_MASK) != 0);
        switch_tab(!backward);
        return TRUE;
    }

    const char *current_tab = main_stack ? gtk_stack_get_visible_child_name(GTK_STACK(main_stack)) : NULL;

    // Handle vim navigation in Stats view: h/Left or l/Right cycles between Today and Weekly
    if (current_tab != NULL && strcmp(current_tab, "stats") == 0) {
        if (keyval == GDK_KEY_h || keyval == GDK_KEY_H || keyval == GDK_KEY_Left) {
            cycle_stats_sub_tab(0);
            return TRUE;
        }
        if (keyval == GDK_KEY_l || keyval == GDK_KEY_L || keyval == GDK_KEY_Right) {
            cycle_stats_sub_tab(1);
            return TRUE;
        }
    }

    // Handle vim navigation in Settings view: k/h to focus work duration, l/j to focus break duration
    if (current_tab != NULL && strcmp(current_tab, "settings") == 0) {
        if (keyval == GDK_KEY_k || keyval == GDK_KEY_K || keyval == GDK_KEY_h || keyval == GDK_KEY_H || keyval == GDK_KEY_Up) {
            switch_setting_entry(-1);
            return TRUE;
        }
        if (keyval == GDK_KEY_l || keyval == GDK_KEY_L || keyval == GDK_KEY_j || keyval == GDK_KEY_J || keyval == GDK_KEY_Down) {
            switch_setting_entry(1);
            return TRUE;
        }
    }

    // Handle vim navigation and enter trigger in Timer view: h/l to cycle buttons, Enter to activate
    if (current_tab != NULL && strcmp(current_tab, "timer") == 0) {
        if (keyval == GDK_KEY_h || keyval == GDK_KEY_H || keyval == GDK_KEY_Left) {
            update_timer_button_focus(focused_timer_button_idx - 1);
            return TRUE;
        }
        if (keyval == GDK_KEY_l || keyval == GDK_KEY_L || keyval == GDK_KEY_Right) {
            update_timer_button_focus(focused_timer_button_idx + 1);
            return TRUE;
        }
        if (keyval == GDK_KEY_Return || keyval == GDK_KEY_KP_Enter) {
            trigger_focused_timer_button();
            return TRUE;
        }
    }

    GtkWidget *win = gtk_event_controller_get_widget(GTK_EVENT_CONTROLLER(controller));
    GtkWidget *focus = GTK_IS_WINDOW(win) ? gtk_window_get_focus(GTK_WINDOW(win)) : NULL;
    gboolean is_editable = (focus != NULL && (GTK_IS_EDITABLE(focus) || GTK_IS_TEXT(focus)));

    if (keyval == GDK_KEY_Escape || (!is_editable && (keyval == GDK_KEY_q || keyval == GDK_KEY_Q))) {
        pom_gtk_quit_async();
        return TRUE;
    }

    if (is_editable) {
        return FALSE;
    }

    switch (keyval) {
    case GDK_KEY_space: goGTKCommand("toggle"); return TRUE;
    case GDK_KEY_s: case GDK_KEY_S: goGTKCommand("skip"); return TRUE;
    case GDK_KEY_r: case GDK_KEY_R: goGTKCommand("reset"); return TRUE;
    case GDK_KEY_x: case GDK_KEY_X: goGTKCommand("stop"); return TRUE;
    default: return FALSE;
    }
}

static gboolean key_pressed_dev(GtkEventControllerKey *controller, guint keyval,
                                guint keycode, GdkModifierType state, gpointer data) {
    (void)keycode;
    (void)data;
    if (keyval == GDK_KEY_Tab || keyval == GDK_KEY_ISO_Left_Tab) {
        gboolean backward = (keyval == GDK_KEY_ISO_Left_Tab) || ((state & GDK_SHIFT_MASK) != 0);
        switch_tab(!backward);
        return TRUE;
    }

    const char *current_tab = main_stack ? gtk_stack_get_visible_child_name(GTK_STACK(main_stack)) : NULL;

    // Handle vim navigation in Stats view: h/Left or l/Right cycles between Today and Weekly
    if (current_tab != NULL && strcmp(current_tab, "stats") == 0) {
        if (keyval == GDK_KEY_h || keyval == GDK_KEY_H || keyval == GDK_KEY_Left) {
            cycle_stats_sub_tab(0);
            return TRUE;
        }
        if (keyval == GDK_KEY_l || keyval == GDK_KEY_L || keyval == GDK_KEY_Right) {
            cycle_stats_sub_tab(1);
            return TRUE;
        }
    }

    // Handle vim navigation in Settings view: k/h to focus work duration, l/j to focus break duration
    if (current_tab != NULL && strcmp(current_tab, "settings") == 0) {
        if (keyval == GDK_KEY_k || keyval == GDK_KEY_K || keyval == GDK_KEY_h || keyval == GDK_KEY_H || keyval == GDK_KEY_Up) {
            switch_setting_entry(-1);
            return TRUE;
        }
        if (keyval == GDK_KEY_l || keyval == GDK_KEY_L || keyval == GDK_KEY_j || keyval == GDK_KEY_J || keyval == GDK_KEY_Down) {
            switch_setting_entry(1);
            return TRUE;
        }
    }

    // Handle vim navigation and enter trigger in Timer view: h/l to cycle buttons, Enter to activate
    if (current_tab != NULL && strcmp(current_tab, "timer") == 0) {
        if (keyval == GDK_KEY_h || keyval == GDK_KEY_H || keyval == GDK_KEY_Left) {
            update_timer_button_focus(focused_timer_button_idx - 1);
            return TRUE;
        }
        if (keyval == GDK_KEY_l || keyval == GDK_KEY_L || keyval == GDK_KEY_Right) {
            update_timer_button_focus(focused_timer_button_idx + 1);
            return TRUE;
        }
        if (keyval == GDK_KEY_Return || keyval == GDK_KEY_KP_Enter) {
            trigger_focused_timer_button();
            return TRUE;
        }
    }

    GtkWidget *win = gtk_event_controller_get_widget(GTK_EVENT_CONTROLLER(controller));
    GtkWidget *focus = GTK_IS_WINDOW(win) ? gtk_window_get_focus(GTK_WINDOW(win)) : NULL;
    gboolean is_editable = (focus != NULL && (GTK_IS_EDITABLE(focus) || GTK_IS_TEXT(focus)));

    if (keyval == GDK_KEY_Escape || (!is_editable && (keyval == GDK_KEY_q || keyval == GDK_KEY_Q))) {
        pom_gtk_quit_async();
        return TRUE;
    }

    if (is_editable) {
        return FALSE;
    }

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
    default: return FALSE;
    }
}

static void activate(GtkApplication *app, gpointer data) {
    (void)data;
    GList *windows = gtk_application_get_windows(app);
    if (windows != NULL) {
        gtk_window_present(GTK_WINDOW(windows->data));
        return;
    }

    GtkWidget *window = gtk_application_window_new(app);
    gtk_window_set_title(GTK_WINDOW(window), "Pomodoro");
    gtk_window_set_default_size(GTK_WINDOW(window), 320, 380);
    gtk_window_set_resizable(GTK_WINDOW(window), FALSE);
    gtk_window_set_decorated(GTK_WINDOW(window), FALSE);
    gtk_widget_set_opacity(window, 0.95);
    gtk_widget_add_css_class(window, "pomodoro-window");

    gtk_window_set_child(GTK_WINDOW(window), build_pomodoro_card());

    GtkEventController *keys = gtk_event_controller_key_new();
    gtk_event_controller_set_propagation_phase(keys, GTK_PHASE_CAPTURE);
    g_signal_connect(keys, "key-pressed", G_CALLBACK(key_pressed), NULL);
    gtk_widget_add_controller(window, keys);
    gtk_window_present(GTK_WINDOW(window));
}

static void activate_dev(GtkApplication *app, gpointer data) {
    (void)data;
    GList *windows = gtk_application_get_windows(app);
    if (windows != NULL) {
        gtk_window_present(GTK_WINDOW(windows->data));
        return;
    }

    GtkWidget *window = gtk_application_window_new(app);
    gtk_window_set_title(GTK_WINDOW(window), "Pomodoro [DEV PREVIEW]");
    gtk_window_set_default_size(GTK_WINDOW(window), 540, 520);
    gtk_window_set_resizable(GTK_WINDOW(window), TRUE);
    gtk_window_set_decorated(GTK_WINDOW(window), TRUE);
    gtk_widget_add_css_class(window, "pomodoro-dev-window");

    GtkWidget *main_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_window_set_child(GTK_WINDOW(window), main_box);

    GtkWidget *top_banner = make_label("DEV PREVIEW  •  320×380 Floating Window Simulation", "dev-banner");
    gtk_widget_set_margin_top(top_banner, 16);
    gtk_widget_set_margin_bottom(top_banner, 12);
    gtk_box_append(GTK_BOX(main_box), top_banner);

    dev_error_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    gtk_widget_add_css_class(dev_error_box, "dev-error-box");
    gtk_widget_set_halign(dev_error_box, GTK_ALIGN_CENTER);
    gtk_widget_set_margin_bottom(dev_error_box, 8);
    dev_error_label = make_label("", "dev-error-text");
    gtk_label_set_wrap(GTK_LABEL(dev_error_label), TRUE);
    gtk_label_set_max_width_chars(GTK_LABEL(dev_error_label), 60);
    gtk_box_append(GTK_BOX(dev_error_box), dev_error_label);
    gtk_widget_set_visible(dev_error_box, FALSE);
    gtk_box_append(GTK_BOX(main_box), dev_error_box);

    GtkWidget *center_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_widget_set_vexpand(center_box, TRUE);
    gtk_widget_set_hexpand(center_box, TRUE);
    gtk_widget_set_valign(center_box, GTK_ALIGN_CENTER);
    gtk_widget_set_halign(center_box, GTK_ALIGN_CENTER);

    GtkWidget *card_frame = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_widget_add_css_class(card_frame, "card-preview");

    GtkWidget *card = build_pomodoro_card();
    if (card != NULL) {
        gtk_box_append(GTK_BOX(card_frame), card);
    } else {
        GtkWidget *err_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 12);
        gtk_widget_set_size_request(err_box, 320, 380);
        gtk_widget_set_valign(err_box, GTK_ALIGN_CENTER);
        gtk_widget_set_halign(err_box, GTK_ALIGN_CENTER);
        GtkWidget *err_lbl = make_label("⚠️ Failed to construct Pomodoro card", "dev-error-text");
        gtk_box_append(GTK_BOX(err_box), err_lbl);
        gtk_box_append(GTK_BOX(card_frame), err_box);
    }

    gtk_box_append(GTK_BOX(center_box), card_frame);
    gtk_box_append(GTK_BOX(main_box), center_box);

    GtkWidget *bottom_banner = make_label("[H/L] Select  •  [Enter] Trigger  •  [K/L] Settings  •  [Tab] Tabs  •  [Space] Play/Pause  •  [Q] Quit", "dev-banner");
    gtk_widget_set_margin_top(bottom_banner, 12);
    gtk_widget_set_margin_bottom(bottom_banner, 16);
    gtk_box_append(GTK_BOX(main_box), bottom_banner);

    GtkEventController *keys = gtk_event_controller_key_new();
    gtk_event_controller_set_propagation_phase(keys, GTK_PHASE_CAPTURE);
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
        ".popup-content { padding: 16px; }"
        ".mode-label { font-size: 12px; font-weight: bold; color: #b4befe; letter-spacing: 1px; }"
        ".mode-label.break-mode { color: #89b4fa; }"
        ".timer-label { font-family: monospace; font-size: 40px; font-weight: bold; color: #cdd6f4; }"
        ".muted-label { color: #9399b2; font-size: 12px; }"
        ".stats-sub-switcher { background: #181825; border-radius: 8px; padding: 2px; }"
        ".stats-sub-switcher button { border: none; border-radius: 6px; padding: 2px 14px; min-height: 22px; min-width: 56px; background: transparent; color: #a6adc8; font-size: 11px; font-weight: bold; outline: none; box-shadow: none; }"
        ".stats-sub-switcher button:checked { background: #313244; color: #b4befe; }"
        ".stats-sub-switcher button:hover { color: #cdd6f4; }"
        ".stats-sub-header { color: #a6adc8; font-size: 11px; font-weight: bold; margin-bottom: 2px; }"
        ".timeline-box { padding: 4px 6px; }"
        ".timeline-row { margin-bottom: 6px; }"
        ".timeline-time { font-family: monospace; font-size: 11px; font-weight: bold; color: #b4befe; }"
        ".timeline-time-end { font-family: monospace; font-size: 10px; color: #6c7086; }"
        ".timeline-card { background: #181825; border-left: 3px solid #b4befe; border-radius: 6px; padding: 5px 8px; }"
        ".timeline-title { font-size: 11px; font-weight: bold; color: #cdd6f4; }"
        ".timeline-subtitle { font-size: 10px; color: #a6adc8; }"
        ".week-row { padding: 3px 6px; border-radius: 6px; min-height: 22px; }"
        ".week-row.is-today { background: rgba(180, 190, 254, 0.08); }"
        ".week-day { font-size: 11px; font-weight: bold; color: #a6adc8; }"
        ".week-day-today { font-size: 11px; font-weight: bold; color: #b4befe; }"
        ".week-bar trough { background: #313244; min-height: 7px; border-radius: 4px; }"
        ".week-bar progress { background: #89b4fa; border-radius: 4px; min-height: 7px; }"
        ".week-bar-today trough { background: #313244; min-height: 7px; border-radius: 4px; }"
        ".week-bar-today progress { background: #b4befe; border-radius: 4px; min-height: 7px; }"
        ".week-time { font-family: monospace; font-size: 11px; color: #a6adc8; }"
        ".week-time-today { font-family: monospace; font-size: 11px; font-weight: bold; color: #b4befe; }"
        "separator { background: #45475a; min-height: 1px; }"
        "button.ctrl-btn { border: none; border-radius: 20px; min-width: 40px; min-height: 40px; padding: 0; background: transparent; color: #a6adc8; outline: none; box-shadow: none; }"
        "button.ctrl-btn image { -gtk-icon-size: 16px; }"
        "button.ctrl-btn:hover { background: #313244; color: #cdd6f4; }"
        "button.ctrl-btn:active { background: #45475a; color: #ffffff; }"
        "button.ctrl-btn.focused { color: #b4befe; background: transparent; border: none; outline: none; box-shadow: none; }"
        "button.ctrl-btn.focused image { -gtk-icon-size: 22px; }"
        "button.ctrl-btn.focused:hover { background: #313244; color: #b4befe; }"
        ".cycle-dots { color: #9399b2; font-size: 13px; letter-spacing: 2px; }"
        ".setting-title { font-size: 13px; font-weight: bold; color: #cdd6f4; }"
        "entry.setting-entry { background: #181825; color: #cdd6f4; border: 1px solid #313244; border-radius: 6px; font-family: monospace; font-size: 13px; font-weight: bold; min-height: 24px; min-width: 32px; padding: 1px 4px; box-shadow: none; outline: none; }"
        "entry.setting-entry:focus-within { border-color: #b4befe; }"
        "entry.setting-entry text { color: #cdd6f4; background: transparent; min-width: 0; min-height: 0; padding: 0; }"
        ".setting-unit { color: #a6adc8; font-size: 12px; font-weight: 500; }"
        ".tab-switcher { background: transparent; border: none; padding: 0; }"
        ".tab-switcher button { border: none; border-bottom: 2px solid transparent; border-radius: 0; padding: 4px 12px; min-height: 28px; min-width: 38px; background: transparent; color: #6c7086; outline: none; box-shadow: none; }"
        ".tab-switcher button:hover { color: #a6adc8; }"
        ".tab-switcher button:checked { background: transparent; color: #cdd6f4; border-bottom: 2px solid #b4befe; }"
        ".dev-error-box { background: rgba(243, 139, 168, 0.15); border: 1px solid #f38ba8; border-radius: 8px; padding: 6px 14px; margin: 0 20px; }"
        ".dev-error-text { color: #f38ba8; font-size: 12px; font-weight: bold; }";

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
    main_stack = NULL;
    stats_sub_stack = NULL;
    stats_today_box = NULL;
    stats_week_box = NULL;
    stats_today_summary_lbl = NULL;
    stats_week_summary_lbl = NULL;
    return G_SOURCE_REMOVE;
}

void pom_gtk_quit_async(void) {
    g_idle_add(quit_application, NULL);
}

int pom_gtk_run(void) {
    is_dev_mode = 0;
    main_stack = NULL;
    stats_sub_stack = NULL;
    stats_today_box = NULL;
    stats_week_box = NULL;
    stats_today_summary_lbl = NULL;
    stats_week_summary_lbl = NULL;
    application = gtk_application_new("io.github.waybarpomodoro.gtk", G_APPLICATION_DEFAULT_FLAGS);
    g_signal_connect(application, "startup", G_CALLBACK(on_startup), NULL);
    g_signal_connect(application, "activate", G_CALLBACK(activate), NULL);
    int status = g_application_run(G_APPLICATION(application), 0, NULL);
    g_object_unref(application);
    application = NULL;
    main_stack = NULL;
    stats_sub_stack = NULL;
    stats_today_box = NULL;
    stats_week_box = NULL;
    stats_today_summary_lbl = NULL;
    stats_week_summary_lbl = NULL;
    return status;
}

int pom_gtk_dev_run(void) {
    is_dev_mode = 1;
    main_stack = NULL;
    stats_sub_stack = NULL;
    stats_today_box = NULL;
    stats_week_box = NULL;
    stats_today_summary_lbl = NULL;
    stats_week_summary_lbl = NULL;
    g_log_set_writer_func(dev_log_writer, NULL, NULL);
    application = gtk_application_new("io.github.waybarpomodoro.gtkdev", G_APPLICATION_DEFAULT_FLAGS);
    g_signal_connect(application, "startup", G_CALLBACK(on_startup), NULL);
    g_signal_connect(application, "activate", G_CALLBACK(activate_dev), NULL);
    int status = g_application_run(G_APPLICATION(application), 0, NULL);
    g_object_unref(application);
    application = NULL;
    main_stack = NULL;
    stats_sub_stack = NULL;
    stats_today_box = NULL;
    stats_week_box = NULL;
    stats_today_summary_lbl = NULL;
    stats_week_summary_lbl = NULL;
    return status;
}

static gboolean fallback_key_pressed(GtkEventControllerKey *controller, guint keyval,
                                     guint keycode, GdkModifierType state, gpointer data) {
    (void)keycode;
    (void)state;
    (void)data;
    if (keyval == GDK_KEY_Escape || keyval == GDK_KEY_q || keyval == GDK_KEY_Q) {
        GtkWidget *win = gtk_event_controller_get_widget(GTK_EVENT_CONTROLLER(controller));
        gtk_window_destroy(GTK_WINDOW(win));
        return TRUE;
    }
    return FALSE;
}

static void activate_fallback(GtkApplication *app, gpointer user_data) {
    const char *err = (const char *)user_data;
    GtkWidget *win = gtk_application_window_new(app);
    gtk_window_set_title(GTK_WINDOW(win), "Pomodoro Dev [ERROR]");
    gtk_window_set_default_size(GTK_WINDOW(win), 480, 240);
    gtk_widget_add_css_class(win, "pomodoro-dev-window");

    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 16);
    gtk_widget_set_margin_top(box, 32);
    gtk_widget_set_margin_bottom(box, 32);
    gtk_widget_set_margin_start(box, 24);
    gtk_widget_set_margin_end(box, 24);
    gtk_widget_set_valign(box, GTK_ALIGN_CENTER);
    gtk_widget_set_halign(box, GTK_ALIGN_CENTER);

    GtkWidget *title = make_label("⚠️ GTK Dev View Error", "stats-title");
    gtk_box_append(GTK_BOX(box), title);

    GtkWidget *err_lbl = make_label(err ? err : "An unexpected GTK error occurred", "dev-error-text");
    gtk_label_set_wrap(GTK_LABEL(err_lbl), TRUE);
    gtk_box_append(GTK_BOX(box), err_lbl);

    GtkWidget *hint = make_label("Press Q or Esc to close", "muted-label");
    gtk_box_append(GTK_BOX(box), hint);

    gtk_window_set_child(GTK_WINDOW(win), box);

    GtkEventController *keys = gtk_event_controller_key_new();
    g_signal_connect(keys, "key-pressed", G_CALLBACK(fallback_key_pressed), NULL);
    gtk_widget_add_controller(win, keys);

    gtk_window_present(GTK_WINDOW(win));
}

void pom_gtk_dev_show_fallback_error(const char *error_msg) {
    GtkApplication *app = gtk_application_new("io.github.waybarpomodoro.fallback", G_APPLICATION_DEFAULT_FLAGS);
    g_signal_connect(app, "startup", G_CALLBACK(on_startup), NULL);
    g_signal_connect(app, "activate", G_CALLBACK(activate_fallback), (gpointer)error_msg);
    g_application_run(G_APPLICATION(app), 0, NULL);
    g_object_unref(app);
}
