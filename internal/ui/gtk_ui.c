#include <gtk/gtk.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <stdint.h>
#include <dlfcn.h>
#include <time.h>

#include "gtk_ui.h"

typedef GtkWidget *(*DevBuildCardFn)(void);
typedef void (*DevReloadCssFn)(void);
typedef void (*DevUpdateFn)(const char *, const char *, const char *, const char *, const char *, const char *, const char *, double);
typedef void (*DevUpdateStatsFn)(const char *, const char *, const char *, const char *);
typedef void (*DevUpdateProjectsFn)(const char *, const char *, const char *);
typedef gboolean (*DevKeyFn)(GtkEventControllerKey *, guint, guint, GdkModifierType, gpointer);

static void *current_dev_module_handle = NULL;
static DevUpdateFn active_update_fn = NULL;
static DevUpdateStatsFn active_update_stats_fn = NULL;
static DevUpdateProjectsFn active_update_projects_fn = NULL;
static DevKeyFn active_key_dev_fn = NULL;
static GtkWidget *dev_card_frame = NULL;

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
static GtkWidget *stats_proj_dropdown = NULL;
static GtkWidget *stats_proj_summary_lbl = NULL;
static GtkWidget *stats_proj_box = NULL;
static char *cached_all_projects = NULL;
static char *cached_project_summaries = NULL;
static char *cached_past_sessions = NULL;
static GtkWidget *timer_buttons[4] = {NULL, NULL, NULL, NULL};
static const char *timer_button_commands[4] = {"toggle", "skip", "reset", "stop"};
static int focused_timer_button_idx = 0;
static GtkWidget *setting_entries[4] = {NULL, NULL, NULL, NULL};
static double current_progress_fraction = 0.0;
static int current_is_break = 0;
static int is_dev_mode = 0;
static GtkWidget *make_label(const char *text, const char *css_class);

static int work_duration_val = 25;
static int break_duration_val = 5;
static int long_break_duration_val = 15;
static int total_cycles_val = 4;

void pom_gtk_set_initial_durations(int work_min, int break_min) {
    if (work_min > 0) work_duration_val = work_min;
    if (break_min > 0) break_duration_val = break_min;
}

void pom_gtk_set_initial_settings(int work_min, int break_min, int long_break_min, int total_cycles) {
    if (work_min > 0) work_duration_val = work_min;
    if (break_min > 0) break_duration_val = break_min;
    if (long_break_min > 0) long_break_duration_val = long_break_min;
    if (total_cycles > 0) total_cycles_val = total_cycles;
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
    current_is_break = (update->mode != NULL && (strcmp(update->mode, "BREAK") == 0 || strcmp(update->mode, "SHORT BREAK") == 0 || strcmp(update->mode, "LONG BREAK") == 0 || strcmp(update->mode, "break") == 0 || strcmp(update->mode, "long_break") == 0));
    if (progress_ring != NULL) {
        gtk_widget_queue_draw(progress_ring);
    }
    if (mode_label != NULL) {
        gtk_widget_remove_css_class(GTK_WIDGET(mode_label), "break-mode");
        gtk_widget_remove_css_class(GTK_WIDGET(mode_label), "work-mode");
        gtk_widget_add_css_class(GTK_WIDGET(mode_label),
                                 current_is_break ? "break-mode" : "work-mode");
    }
    if (timer_buttons[0] != NULL) {
        int is_running = (update->status != NULL && strcmp(update->status, "Running") == 0);
        const char *icon_name = is_running ? "media-playback-pause-symbolic" : "media-playback-start-symbolic";
        gtk_button_set_icon_name(GTK_BUTTON(timer_buttons[0]), icon_name);
        GtkWidget *child = gtk_button_get_child(GTK_BUTTON(timer_buttons[0]));
        if (child != NULL && GTK_IS_IMAGE(child)) {
            int pixel_size = (focused_timer_button_idx == 0) ? 22 : 16;
            gtk_image_set_pixel_size(GTK_IMAGE(child), pixel_size);
        }
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
    if (active_update_fn != NULL) {
        active_update_fn(mode, timer, status, progress, cycle, stats, history, fraction);
        return;
    }
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

typedef struct {
    int block_idx;
    char *orig_start;
    char *orig_end;
    int orig_dur;
    char *orig_proj;
    GtkWidget *popover;
    GtkWidget *start_entry;
    GtkWidget *end_entry;
    GtkWidget *dur_entry;
    GtkWidget *proj_entry;
    gboolean is_updating;
} EditSessionData;

static void free_edit_session_data(gpointer data) {
    EditSessionData *d = (EditSessionData *)data;
    if (d != NULL) {
        g_free(d->orig_start);
        g_free(d->orig_end);
        g_free(d->orig_proj);
        g_free(d);
    }
}

static void on_edit_session_time_changed(GtkEditable *editable, gpointer user_data) {
    (void)editable;
    EditSessionData *d = (EditSessionData *)user_data;
    if (d == NULL || d->is_updating) return;

    const char *st = gtk_editable_get_text(GTK_EDITABLE(d->start_entry));
    const char *et = gtk_editable_get_text(GTK_EDITABLE(d->end_entry));
    if (st == NULL || et == NULL || strlen(st) < 4 || strlen(et) < 4) return;

    int sh = 0, sm = 0, eh = 0, em = 0;
    if (sscanf(st, "%d:%d", &sh, &sm) == 2 && sscanf(et, "%d:%d", &eh, &em) == 2) {
        int diff = (eh * 60 + em) - (sh * 60 + sm);
        if (diff < 0) diff += 24 * 60;
        if (diff > 0 && diff <= 1440) {
            d->is_updating = TRUE;
            char buf[16];
            snprintf(buf, sizeof(buf), "%d", diff);
            gtk_editable_set_text(GTK_EDITABLE(d->dur_entry), buf);
            d->is_updating = FALSE;
        }
    }
}

static void on_edit_session_dur_changed(GtkEditable *editable, gpointer user_data) {
    (void)editable;
    EditSessionData *d = (EditSessionData *)user_data;
    if (d == NULL || d->is_updating) return;

    const char *st = gtk_editable_get_text(GTK_EDITABLE(d->start_entry));
    const char *dt = gtk_editable_get_text(GTK_EDITABLE(d->dur_entry));
    if (st == NULL || dt == NULL || strlen(st) < 4 || strlen(dt) == 0) return;

    int sh = 0, sm = 0;
    int dur = atoi(dt);
    if (sscanf(st, "%d:%d", &sh, &sm) == 2 && dur > 0 && dur <= 1440) {
        int total_m = (sh * 60 + sm + dur) % (24 * 60);
        int eh = total_m / 60;
        int em = total_m % 60;
        d->is_updating = TRUE;
        char buf[16];
        snprintf(buf, sizeof(buf), "%02d:%02d", eh, em);
        gtk_editable_set_text(GTK_EDITABLE(d->end_entry), buf);
        d->is_updating = FALSE;
    }
}

static void on_edit_project_chip_clicked(GtkButton *btn, gpointer user_data) {
    EditSessionData *d = (EditSessionData *)user_data;
    const char *label = gtk_button_get_label(btn);
    if (label != NULL && d != NULL && d->proj_entry != NULL) {
        gtk_editable_set_text(GTK_EDITABLE(d->proj_entry), label);
    }
}

static void edit_session_submit(GtkWidget *widget, gpointer user_data) {
    (void)widget;
    EditSessionData *d = (EditSessionData *)user_data;
    if (d == NULL) return;

    const char *st = gtk_editable_get_text(GTK_EDITABLE(d->start_entry));
    const char *et = gtk_editable_get_text(GTK_EDITABLE(d->end_entry));
    const char *dt = gtk_editable_get_text(GTK_EDITABLE(d->dur_entry));
    const char *pt = gtk_editable_get_text(GTK_EDITABLE(d->proj_entry));

    char start_buf[16] = "00:00";
    char end_buf[16] = "00:00";
    int dur = (dt != NULL) ? atoi(dt) : 0;

    if (st != NULL && strlen(st) >= 3) {
        strncpy(start_buf, st, sizeof(start_buf) - 1);
    } else if (d->orig_start != NULL && strlen(d->orig_start) >= 3) {
        strncpy(start_buf, d->orig_start, sizeof(start_buf) - 1);
    }

    if (et != NULL && strlen(et) >= 3) {
        strncpy(end_buf, et, sizeof(end_buf) - 1);
    } else if (d->orig_end != NULL && strlen(d->orig_end) >= 3) {
        strncpy(end_buf, d->orig_end, sizeof(end_buf) - 1);
    }

    if (dur <= 0) {
        int sh = 0, sm = 0, eh = 0, em = 0;
        if (sscanf(start_buf, "%d:%d", &sh, &sm) == 2 && sscanf(end_buf, "%d:%d", &eh, &em) == 2) {
            dur = (eh * 60 + em) - (sh * 60 + sm);
            if (dur < 0) dur += 24 * 60;
        }
        if (dur <= 0) dur = (d->orig_dur > 0) ? d->orig_dur : 25;
    }

    const char *proj_str = (pt != NULL && strlen(pt) > 0) ? pt : "-";

    char *cmd = g_strdup_printf("edit_block %d %s %s %d %s", d->block_idx, start_buf, end_buf, dur, proj_str);

    if (is_dev_mode) {
        goGTKDevAction(cmd);
    } else {
        goGTKCommand(cmd);
    }
    g_free(cmd);

    if (d->popover != NULL) {
        gtk_popover_popdown(GTK_POPOVER(d->popover));
    }
}

static void on_edit_session_popover_show(GtkWidget *popover, gpointer user_data) {
    (void)popover;
    EditSessionData *d = (EditSessionData *)user_data;
    if (d == NULL) return;

    d->is_updating = TRUE;
    gtk_editable_set_text(GTK_EDITABLE(d->start_entry), d->orig_start ? d->orig_start : "");
    gtk_editable_set_text(GTK_EDITABLE(d->end_entry), d->orig_end ? d->orig_end : "");
    char dur_buf[16];
    snprintf(dur_buf, sizeof(dur_buf), "%d", d->orig_dur > 0 ? d->orig_dur : 25);
    gtk_editable_set_text(GTK_EDITABLE(d->dur_entry), dur_buf);
    gtk_editable_set_text(GTK_EDITABLE(d->proj_entry), d->orig_proj ? d->orig_proj : "");
    d->is_updating = FALSE;
}

static GtkWidget *create_edit_session_popover(int block_idx, const char *start_time, const char *end_time, int dur_min, const char *current_proj) {
    GtkWidget *pop = gtk_popover_new();
    gtk_widget_add_css_class(pop, "session-popover");
    gtk_popover_set_position(GTK_POPOVER(pop), GTK_POS_BOTTOM);

    GtkWidget *content = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    gtk_widget_set_size_request(content, 220, -1);
    gtk_widget_set_margin_start(content, 10);
    gtk_widget_set_margin_end(content, 10);
    gtk_widget_set_margin_top(content, 10);
    gtk_widget_set_margin_bottom(content, 10);

    GtkWidget *title = make_label("Edit Session", "project-popover-title");
    gtk_label_set_xalign(GTK_LABEL(title), 0.0f);
    gtk_box_append(GTK_BOX(content), title);

    GtkWidget *sep = gtk_separator_new(GTK_ORIENTATION_HORIZONTAL);
    gtk_box_append(GTK_BOX(content), sep);

    EditSessionData *data = g_new0(EditSessionData, 1);
    data->block_idx = block_idx;
    data->orig_start = g_strdup(start_time ? start_time : "");
    data->orig_end = g_strdup(end_time ? end_time : "");
    data->orig_dur = dur_min;
    data->orig_proj = g_strdup(current_proj ? current_proj : "");
    data->popover = pop;

    GtkWidget *times_row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);

    GtkWidget *start_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 3);
    gtk_widget_set_hexpand(start_box, TRUE);
    GtkWidget *start_lbl = make_label("Start Time", "setting-unit");
    gtk_label_set_xalign(GTK_LABEL(start_lbl), 0.0f);
    data->start_entry = gtk_entry_new();
    gtk_widget_add_css_class(data->start_entry, "session-entry");
    gtk_editable_set_text(GTK_EDITABLE(data->start_entry), data->orig_start);
    gtk_entry_set_max_length(GTK_ENTRY(data->start_entry), 5);
    gtk_box_append(GTK_BOX(start_box), start_lbl);
    gtk_box_append(GTK_BOX(start_box), data->start_entry);

    GtkWidget *end_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 3);
    gtk_widget_set_hexpand(end_box, TRUE);
    GtkWidget *end_lbl = make_label("End Time", "setting-unit");
    gtk_label_set_xalign(GTK_LABEL(end_lbl), 0.0f);
    data->end_entry = gtk_entry_new();
    gtk_widget_add_css_class(data->end_entry, "session-entry");
    gtk_editable_set_text(GTK_EDITABLE(data->end_entry), data->orig_end);
    gtk_entry_set_max_length(GTK_ENTRY(data->end_entry), 5);
    gtk_box_append(GTK_BOX(end_box), end_lbl);
    gtk_box_append(GTK_BOX(end_box), data->end_entry);

    gtk_box_append(GTK_BOX(times_row), start_box);
    gtk_box_append(GTK_BOX(times_row), end_box);
    gtk_box_append(GTK_BOX(content), times_row);

    GtkWidget *dur_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 3);
    GtkWidget *dur_lbl = make_label("Duration (min)", "setting-unit");
    gtk_label_set_xalign(GTK_LABEL(dur_lbl), 0.0f);
    data->dur_entry = gtk_entry_new();
    gtk_widget_add_css_class(data->dur_entry, "session-entry");
    char dur_buf[16];
    snprintf(dur_buf, sizeof(dur_buf), "%d", dur_min > 0 ? dur_min : 25);
    gtk_editable_set_text(GTK_EDITABLE(data->dur_entry), dur_buf);
    gtk_entry_set_max_length(GTK_ENTRY(data->dur_entry), 4);
    gtk_box_append(GTK_BOX(dur_box), dur_lbl);
    gtk_box_append(GTK_BOX(dur_box), data->dur_entry);
    gtk_box_append(GTK_BOX(content), dur_box);

    GtkWidget *proj_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 3);
    GtkWidget *proj_lbl = make_label("Project", "setting-unit");
    gtk_label_set_xalign(GTK_LABEL(proj_lbl), 0.0f);
    data->proj_entry = gtk_entry_new();
    gtk_widget_add_css_class(data->proj_entry, "session-entry");
    gtk_editable_set_text(GTK_EDITABLE(data->proj_entry), data->orig_proj);
    gtk_entry_set_placeholder_text(GTK_ENTRY(data->proj_entry), "Project (optional)...");
    gtk_box_append(GTK_BOX(proj_box), proj_lbl);
    gtk_box_append(GTK_BOX(proj_box), data->proj_entry);
    gtk_box_append(GTK_BOX(content), proj_box);

    if (cached_all_projects != NULL && strlen(cached_all_projects) > 0) {
        char **projs = g_strsplit(cached_all_projects, "\n", -1);
        GtkWidget *chip_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 4);
        int added = 0;
        for (int i = 0; projs[i] != NULL && added < 4; i++) {
            if (strlen(projs[i]) == 0) continue;
            GtkWidget *chip = gtk_button_new_with_label(projs[i]);
            gtk_widget_add_css_class(chip, "project-chip-btn");
            if (current_proj != NULL && strcmp(current_proj, projs[i]) == 0) {
                gtk_widget_add_css_class(chip, "selected");
            }
            g_signal_connect(chip, "clicked", G_CALLBACK(on_edit_project_chip_clicked), data);
            gtk_box_append(GTK_BOX(chip_box), chip);
            added++;
        }
        if (added > 0) {
            gtk_box_append(GTK_BOX(content), chip_box);
        }
        g_strfreev(projs);
    }

    g_signal_connect(data->start_entry, "changed", G_CALLBACK(on_edit_session_time_changed), data);
    g_signal_connect(data->end_entry, "changed", G_CALLBACK(on_edit_session_time_changed), data);
    g_signal_connect(data->dur_entry, "changed", G_CALLBACK(on_edit_session_dur_changed), data);

    g_signal_connect(data->start_entry, "activate", G_CALLBACK(edit_session_submit), data);
    g_signal_connect(data->end_entry, "activate", G_CALLBACK(edit_session_submit), data);
    g_signal_connect(data->dur_entry, "activate", G_CALLBACK(edit_session_submit), data);
    g_signal_connect(data->proj_entry, "activate", G_CALLBACK(edit_session_submit), data);

    GtkWidget *save_btn = gtk_button_new_with_label("Save Changes");
    gtk_widget_add_css_class(save_btn, "primary-button");
    gtk_widget_add_css_class(save_btn, "session-submit-btn");
    gtk_widget_set_size_request(save_btn, -1, 30);
    g_signal_connect(save_btn, "clicked", G_CALLBACK(edit_session_submit), data);
    gtk_box_append(GTK_BOX(content), save_btn);

    g_signal_connect(pop, "show", G_CALLBACK(on_edit_session_popover_show), data);
    g_object_set_data_full(G_OBJECT(pop), "edit_session_data", data, free_edit_session_data);

    gtk_popover_set_child(GTK_POPOVER(pop), content);
    return pop;
}

typedef struct {
    GtkWidget *popover;
    GtkWidget *start_entry;
    GtkWidget *end_entry;
    GtkWidget *dur_entry;
    GtkWidget *proj_entry;
    gboolean is_updating;
} AddSessionData;

static void free_add_session_data(gpointer data) {
    g_free(data);
}

static void on_add_session_time_changed(GtkEditable *editable, gpointer user_data) {
    (void)editable;
    AddSessionData *d = (AddSessionData *)user_data;
    if (d == NULL || d->is_updating) return;

    const char *st = gtk_editable_get_text(GTK_EDITABLE(d->start_entry));
    const char *et = gtk_editable_get_text(GTK_EDITABLE(d->end_entry));
    if (st == NULL || et == NULL || strlen(st) < 4 || strlen(et) < 4) return;

    int sh = 0, sm = 0, eh = 0, em = 0;
    if (sscanf(st, "%d:%d", &sh, &sm) == 2 && sscanf(et, "%d:%d", &eh, &em) == 2) {
        int diff = (eh * 60 + em) - (sh * 60 + sm);
        if (diff < 0) diff += 24 * 60;
        if (diff > 0 && diff <= 1440) {
            d->is_updating = TRUE;
            char buf[16];
            snprintf(buf, sizeof(buf), "%d", diff);
            gtk_editable_set_text(GTK_EDITABLE(d->dur_entry), buf);
            d->is_updating = FALSE;
        }
    }
}

static void on_add_session_dur_changed(GtkEditable *editable, gpointer user_data) {
    (void)editable;
    AddSessionData *d = (AddSessionData *)user_data;
    if (d == NULL || d->is_updating) return;

    const char *st = gtk_editable_get_text(GTK_EDITABLE(d->start_entry));
    const char *dt = gtk_editable_get_text(GTK_EDITABLE(d->dur_entry));
    if (st == NULL || dt == NULL || strlen(st) < 4 || strlen(dt) == 0) return;

    int sh = 0, sm = 0;
    int dur = atoi(dt);
    if (sscanf(st, "%d:%d", &sh, &sm) == 2 && dur > 0 && dur <= 1440) {
        int total_m = (sh * 60 + sm + dur) % (24 * 60);
        int eh = total_m / 60;
        int em = total_m % 60;
        d->is_updating = TRUE;
        char buf[16];
        snprintf(buf, sizeof(buf), "%02d:%02d", eh, em);
        gtk_editable_set_text(GTK_EDITABLE(d->end_entry), buf);
        d->is_updating = FALSE;
    }
}

static void on_project_chip_clicked(GtkButton *btn, gpointer user_data) {
    AddSessionData *d = (AddSessionData *)user_data;
    const char *label = gtk_button_get_label(btn);
    if (label != NULL && d != NULL && d->proj_entry != NULL) {
        gtk_editable_set_text(GTK_EDITABLE(d->proj_entry), label);
    }
}

static void add_session_submit(GtkWidget *widget, gpointer user_data) {
    (void)widget;
    AddSessionData *d = (AddSessionData *)user_data;
    if (d == NULL) return;

    const char *st = gtk_editable_get_text(GTK_EDITABLE(d->start_entry));
    const char *et = gtk_editable_get_text(GTK_EDITABLE(d->end_entry));
    const char *dt = gtk_editable_get_text(GTK_EDITABLE(d->dur_entry));
    const char *pt = gtk_editable_get_text(GTK_EDITABLE(d->proj_entry));

    char start_buf[16] = "00:00";
    char end_buf[16] = "00:00";
    int dur = (dt != NULL) ? atoi(dt) : 0;

    time_t now_t = time(NULL);
    struct tm *tm_now = localtime(&now_t);

    if (st != NULL && strlen(st) >= 3) {
        strncpy(start_buf, st, sizeof(start_buf) - 1);
    } else {
        snprintf(start_buf, sizeof(start_buf), "%02d:%02d", tm_now->tm_hour, tm_now->tm_min);
    }

    if (et != NULL && strlen(et) >= 3) {
        strncpy(end_buf, et, sizeof(end_buf) - 1);
    } else {
        snprintf(end_buf, sizeof(end_buf), "%02d:%02d", tm_now->tm_hour, tm_now->tm_min);
    }

    if (dur <= 0) {
        int sh = 0, sm = 0, eh = 0, em = 0;
        if (sscanf(start_buf, "%d:%d", &sh, &sm) == 2 && sscanf(end_buf, "%d:%d", &eh, &em) == 2) {
            dur = (eh * 60 + em) - (sh * 60 + sm);
            if (dur < 0) dur += 24 * 60;
        }
        if (dur <= 0) dur = 25;
    }

    char *cmd = NULL;
    if (pt != NULL && strlen(pt) > 0) {
        cmd = g_strdup_printf("add_session %s %s %d %s", start_buf, end_buf, dur, pt);
    } else {
        cmd = g_strdup_printf("add_session %s %s %d", start_buf, end_buf, dur);
    }

    if (is_dev_mode) {
        goGTKDevAction(cmd);
    } else {
        goGTKCommand(cmd);
    }
    g_free(cmd);

    if (d->popover != NULL) {
        gtk_popover_popdown(GTK_POPOVER(d->popover));
    }
}

static void on_add_session_popover_show(GtkWidget *popover, gpointer user_data) {
    (void)popover;
    AddSessionData *d = (AddSessionData *)user_data;
    if (d == NULL) return;

    time_t now_t = time(NULL);
    struct tm *tm_now = localtime(&now_t);
    char end_def[16];
    snprintf(end_def, sizeof(end_def), "%02d:%02d", tm_now->tm_hour, tm_now->tm_min);

    time_t start_t = now_t - 25 * 60;
    struct tm *tm_start = localtime(&start_t);
    char start_def[16];
    snprintf(start_def, sizeof(start_def), "%02d:%02d", tm_start->tm_hour, tm_start->tm_min);

    d->is_updating = TRUE;
    gtk_editable_set_text(GTK_EDITABLE(d->start_entry), start_def);
    gtk_editable_set_text(GTK_EDITABLE(d->end_entry), end_def);
    gtk_editable_set_text(GTK_EDITABLE(d->dur_entry), "25");
    gtk_editable_set_text(GTK_EDITABLE(d->proj_entry), "");
    d->is_updating = FALSE;
}

static GtkWidget *create_add_session_popover(void) {
    GtkWidget *pop = gtk_popover_new();
    gtk_widget_add_css_class(pop, "session-popover");
    gtk_popover_set_position(GTK_POPOVER(pop), GTK_POS_BOTTOM);

    GtkWidget *content = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    gtk_widget_set_size_request(content, 220, -1);
    gtk_widget_set_margin_start(content, 10);
    gtk_widget_set_margin_end(content, 10);
    gtk_widget_set_margin_top(content, 10);
    gtk_widget_set_margin_bottom(content, 10);

    GtkWidget *title = make_label("Add Manual Session", "project-popover-title");
    gtk_label_set_xalign(GTK_LABEL(title), 0.0f);
    gtk_box_append(GTK_BOX(content), title);

    GtkWidget *sep = gtk_separator_new(GTK_ORIENTATION_HORIZONTAL);
    gtk_box_append(GTK_BOX(content), sep);

    AddSessionData *data = g_new0(AddSessionData, 1);
    data->popover = pop;

    time_t now_t = time(NULL);
    struct tm *tm_now = localtime(&now_t);
    char end_def[16];
    snprintf(end_def, sizeof(end_def), "%02d:%02d", tm_now->tm_hour, tm_now->tm_min);

    time_t start_t = now_t - 25 * 60;
    struct tm *tm_start = localtime(&start_t);
    char start_def[16];
    snprintf(start_def, sizeof(start_def), "%02d:%02d", tm_start->tm_hour, tm_start->tm_min);

    GtkWidget *times_row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);

    GtkWidget *start_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 3);
    gtk_widget_set_hexpand(start_box, TRUE);
    GtkWidget *start_lbl = make_label("Start Time", "setting-unit");
    gtk_label_set_xalign(GTK_LABEL(start_lbl), 0.0f);
    data->start_entry = gtk_entry_new();
    gtk_widget_add_css_class(data->start_entry, "session-entry");
    gtk_editable_set_text(GTK_EDITABLE(data->start_entry), start_def);
    gtk_entry_set_max_length(GTK_ENTRY(data->start_entry), 5);
    gtk_box_append(GTK_BOX(start_box), start_lbl);
    gtk_box_append(GTK_BOX(start_box), data->start_entry);

    GtkWidget *end_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 3);
    gtk_widget_set_hexpand(end_box, TRUE);
    GtkWidget *end_lbl = make_label("End Time", "setting-unit");
    gtk_label_set_xalign(GTK_LABEL(end_lbl), 0.0f);
    data->end_entry = gtk_entry_new();
    gtk_widget_add_css_class(data->end_entry, "session-entry");
    gtk_editable_set_text(GTK_EDITABLE(data->end_entry), end_def);
    gtk_entry_set_max_length(GTK_ENTRY(data->end_entry), 5);
    gtk_box_append(GTK_BOX(end_box), end_lbl);
    gtk_box_append(GTK_BOX(end_box), data->end_entry);

    gtk_box_append(GTK_BOX(times_row), start_box);
    gtk_box_append(GTK_BOX(times_row), end_box);
    gtk_box_append(GTK_BOX(content), times_row);

    GtkWidget *dur_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 3);
    GtkWidget *dur_lbl = make_label("Duration (min)", "setting-unit");
    gtk_label_set_xalign(GTK_LABEL(dur_lbl), 0.0f);
    data->dur_entry = gtk_entry_new();
    gtk_widget_add_css_class(data->dur_entry, "session-entry");
    gtk_editable_set_text(GTK_EDITABLE(data->dur_entry), "25");
    gtk_entry_set_max_length(GTK_ENTRY(data->dur_entry), 4);
    gtk_box_append(GTK_BOX(dur_box), dur_lbl);
    gtk_box_append(GTK_BOX(dur_box), data->dur_entry);
    gtk_box_append(GTK_BOX(content), dur_box);

    GtkWidget *proj_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 3);
    GtkWidget *proj_lbl = make_label("Project", "setting-unit");
    gtk_label_set_xalign(GTK_LABEL(proj_lbl), 0.0f);
    data->proj_entry = gtk_entry_new();
    gtk_widget_add_css_class(data->proj_entry, "session-entry");
    gtk_entry_set_placeholder_text(GTK_ENTRY(data->proj_entry), "Project (optional)...");
    gtk_box_append(GTK_BOX(proj_box), proj_lbl);
    gtk_box_append(GTK_BOX(proj_box), data->proj_entry);
    gtk_box_append(GTK_BOX(content), proj_box);

    if (cached_all_projects != NULL && strlen(cached_all_projects) > 0) {
        char **projs = g_strsplit(cached_all_projects, "\n", -1);
        GtkWidget *chip_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 4);
        int added = 0;
        for (int i = 0; projs[i] != NULL && added < 4; i++) {
            if (strlen(projs[i]) == 0) continue;
            GtkWidget *chip = gtk_button_new_with_label(projs[i]);
            gtk_widget_add_css_class(chip, "project-chip-btn");
            g_signal_connect(chip, "clicked", G_CALLBACK(on_project_chip_clicked), data);
            gtk_box_append(GTK_BOX(chip_box), chip);
            added++;
        }
        if (added > 0) {
            gtk_box_append(GTK_BOX(content), chip_box);
        }
        g_strfreev(projs);
    }

    g_signal_connect(data->start_entry, "changed", G_CALLBACK(on_add_session_time_changed), data);
    g_signal_connect(data->end_entry, "changed", G_CALLBACK(on_add_session_time_changed), data);
    g_signal_connect(data->dur_entry, "changed", G_CALLBACK(on_add_session_dur_changed), data);

    g_signal_connect(data->start_entry, "activate", G_CALLBACK(add_session_submit), data);
    g_signal_connect(data->end_entry, "activate", G_CALLBACK(add_session_submit), data);
    g_signal_connect(data->dur_entry, "activate", G_CALLBACK(add_session_submit), data);
    g_signal_connect(data->proj_entry, "activate", G_CALLBACK(add_session_submit), data);

    GtkWidget *submit_btn = gtk_button_new_with_label("Add Session");
    gtk_widget_add_css_class(submit_btn, "primary-button");
    gtk_widget_add_css_class(submit_btn, "session-submit-btn");
    gtk_widget_set_size_request(submit_btn, -1, 30);
    g_signal_connect(submit_btn, "clicked", G_CALLBACK(add_session_submit), data);
    gtk_box_append(GTK_BOX(content), submit_btn);

    g_signal_connect(pop, "show", G_CALLBACK(on_add_session_popover_show), data);
    g_object_set_data_full(G_OBJECT(pop), "add_session_data", data, free_add_session_data);

    gtk_popover_set_child(GTK_POPOVER(pop), content);
    return pop;
}

static void delete_block_clicked(GtkButton *button, gpointer user_data) {
    (void)button;
    char *cmd = (char *)user_data;
    if (cmd != NULL) {
        if (is_dev_mode) {
            goGTKDevAction(cmd);
        } else {
            goGTKCommand(cmd);
        }
    }
}

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
            GtkWidget *empty_lbl = gtk_label_new("No focus sessions recorded today");
            gtk_widget_add_css_class(empty_lbl, "muted-label");
            gtk_widget_set_margin_top(empty_lbl, 28);
            gtk_box_append(GTK_BOX(stats_today_box), empty_lbl);
        } else {
            char **lines = g_strsplit(up->today_blocks, "\n", -1);
            for (int i = 0; lines[i] != NULL; i++) {
                if (strlen(lines[i]) == 0) continue;
                char **parts = g_strsplit(lines[i], "|", -1);
                guint n_parts = g_strv_length(parts);
                if (n_parts >= 3) {
                    int block_idx = i;
                    const char *start = "";
                    const char *end = "";
                    const char *dur = "0m";
                    const char *proj = "";

                    if (n_parts >= 4) {
                        block_idx = atoi(parts[0]);
                        start = parts[1];
                        end = parts[2];
                        dur = parts[3];
                        if (n_parts >= 5 && strlen(parts[4]) > 0) {
                            proj = parts[4];
                        }
                    } else {
                        block_idx = i;
                        start = parts[0];
                        end = parts[1];
                        dur = parts[2];
                    }

                    int dur_min = 0;
                    if (n_parts >= 6 && strlen(parts[5]) > 0) {
                        dur_min = atoi(parts[5]);
                    }
                    if (dur_min <= 0 && strlen(start) >= 4 && strlen(end) >= 4) {
                        int sh = 0, sm = 0, eh = 0, em = 0;
                        if (sscanf(start, "%d:%d", &sh, &sm) == 2 && sscanf(end, "%d:%d", &eh, &em) == 2) {
                            dur_min = (eh * 60 + em) - (sh * 60 + sm);
                            if (dur_min < 0) dur_min += 24 * 60;
                        }
                    }
                    if (dur_min <= 0) dur_min = 25;

                    GtkWidget *row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
                    gtk_widget_add_css_class(row, "timeline-row");
                    gtk_widget_set_valign(row, GTK_ALIGN_CENTER);

                    GtkWidget *dot = gtk_label_new("●");
                    gtk_widget_add_css_class(dot, "timeline-dot");

                    char range_buf[64];
                    snprintf(range_buf, sizeof(range_buf), "%s – %s", start, end);
                    GtkWidget *time_lbl = gtk_label_new(range_buf);
                    gtk_widget_add_css_class(time_lbl, "timeline-time-range");
                    gtk_label_set_xalign(GTK_LABEL(time_lbl), 0.0f);

                    gtk_box_append(GTK_BOX(row), dot);
                    gtk_box_append(GTK_BOX(row), time_lbl);

                    if (strlen(proj) > 0) {
                        char short_proj[8];
                        g_utf8_strncpy(short_proj, proj, 4);
                        GtkWidget *p_badge = gtk_label_new(short_proj);
                        gtk_widget_add_css_class(p_badge, "timeline-project-badge");
                        gtk_widget_set_tooltip_text(p_badge, proj);
                        gtk_box_append(GTK_BOX(row), p_badge);
                    }

                    GtkWidget *dur_lbl = gtk_label_new(dur);
                    gtk_widget_add_css_class(dur_lbl, "timeline-dur");
                    gtk_widget_set_hexpand(dur_lbl, TRUE);
                    gtk_label_set_xalign(GTK_LABEL(dur_lbl), 1.0f);

                    GtkWidget *edit_btn = gtk_menu_button_new();
                    gtk_menu_button_set_icon_name(GTK_MENU_BUTTON(edit_btn), "document-edit-symbolic");
                    gtk_widget_add_css_class(edit_btn, "flat");
                    gtk_widget_add_css_class(edit_btn, "timeline-edit-btn");
                    GtkWidget *edit_child = gtk_widget_get_first_child(edit_btn);
                    if (edit_child != NULL) {
                        gtk_widget_add_css_class(edit_child, "flat");
                        gtk_widget_add_css_class(edit_child, "timeline-edit-btn");
                    }
                    GtkWidget *edit_img = gtk_button_get_child(GTK_BUTTON(edit_child && GTK_IS_BUTTON(edit_child) ? edit_child : edit_btn));
                    if (edit_img != NULL && GTK_IS_IMAGE(edit_img)) {
                        gtk_image_set_pixel_size(GTK_IMAGE(edit_img), 14);
                    }
                    gtk_widget_set_tooltip_text(edit_btn, "Edit session");
                    gtk_widget_set_size_request(edit_btn, 24, 24);
                    gtk_widget_set_valign(edit_btn, GTK_ALIGN_CENTER);

                    GtkWidget *popover = create_edit_session_popover(block_idx, start, end, dur_min, proj);
                    gtk_menu_button_set_popover(GTK_MENU_BUTTON(edit_btn), popover);

                    GtkWidget *del_btn = gtk_button_new_from_icon_name("user-trash-symbolic");
                    GtkWidget *btn_child = gtk_button_get_child(GTK_BUTTON(del_btn));
                    if (btn_child != NULL && GTK_IS_IMAGE(btn_child)) {
                        gtk_image_set_pixel_size(GTK_IMAGE(btn_child), 14);
                    }
                    gtk_widget_add_css_class(del_btn, "timeline-delete-btn");
                    gtk_widget_set_tooltip_text(del_btn, "Delete block");
                    gtk_widget_set_size_request(del_btn, 24, 24);
                    gtk_widget_set_valign(del_btn, GTK_ALIGN_CENTER);

                    char *cmd_str = g_strdup_printf("delete_block %d", block_idx);
                    g_signal_connect_data(del_btn, "clicked", G_CALLBACK(delete_block_clicked), cmd_str, (GClosureNotify)g_free, 0);

                    gtk_box_append(GTK_BOX(row), dur_lbl);
                    gtk_box_append(GTK_BOX(row), edit_btn);
                    gtk_box_append(GTK_BOX(row), del_btn);

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
    if (active_update_stats_fn != NULL) {
        active_update_stats_fn(today_summary, today_blocks_data, week_summary, week_days_data);
        return;
    }
    StatsUpdate *up = g_new0(StatsUpdate, 1);
    up->today_summary = g_strdup(today_summary);
    up->today_blocks = g_strdup(today_blocks_data);
    up->week_summary = g_strdup(week_summary);
    up->week_days = g_strdup(week_days_data);
    g_idle_add(apply_stats_update, up);
}

typedef struct {
    char *all_projects;
    char *project_summaries;
    char *past_sessions;
} ProjectsUpdate;

static void on_project_filter_dropdown_changed(GObject *gobject, GParamSpec *pspec, gpointer user_data);

static void on_project_card_clicked(GtkButton *btn, gpointer user_data) {
    (void)btn;
    char *target = (char *)user_data;
    if (stats_proj_dropdown != NULL && target != NULL) {
        GListModel *model = gtk_drop_down_get_model(GTK_DROP_DOWN(stats_proj_dropdown));
        if (model != NULL) {
            guint n = g_list_model_get_n_items(model);
            for (guint i = 1; i < n; i++) {
                GtkStringObject *strobj = GTK_STRING_OBJECT(g_list_model_get_item(model, i));
                if (strobj != NULL) {
                    const char *s = gtk_string_object_get_string(strobj);
                    if (s != NULL && strcmp(s, target) == 0) {
                        gtk_drop_down_set_selected(GTK_DROP_DOWN(stats_proj_dropdown), i);
                        g_object_unref(strobj);
                        break;
                    }
                    g_object_unref(strobj);
                }
            }
        }
    }
}

static void render_projects_view(void) {
    if (stats_proj_box == NULL) return;

    GtkWidget *c = gtk_widget_get_first_child(stats_proj_box);
    while (c != NULL) {
        GtkWidget *next = gtk_widget_get_next_sibling(c);
        gtk_box_remove(GTK_BOX(stats_proj_box), c);
        c = next;
    }

    guint sel = stats_proj_dropdown ? gtk_drop_down_get_selected(GTK_DROP_DOWN(stats_proj_dropdown)) : 0;

    if (sel == 0) {
        // "All Projects" view: aggregated summary per project
        if (cached_project_summaries == NULL || strlen(cached_project_summaries) == 0) {
            if (stats_proj_summary_lbl != NULL) {
                gtk_label_set_text(GTK_LABEL(stats_proj_summary_lbl), "All Projects: 0m total");
            }
            GtkWidget *empty_lbl = gtk_label_new("No projects recorded yet");
            gtk_widget_add_css_class(empty_lbl, "muted-label");
            gtk_widget_set_margin_top(empty_lbl, 28);
            gtk_box_append(GTK_BOX(stats_proj_box), empty_lbl);

            GtkWidget *sub_lbl = gtk_label_new("Attach a project using the edit icon in Today!");
            gtk_widget_add_css_class(sub_lbl, "muted-label");
            gtk_widget_set_margin_top(sub_lbl, 4);
            gtk_box_append(GTK_BOX(stats_proj_box), sub_lbl);
            return;
        }

        char **lines = g_strsplit(cached_project_summaries, "\n", -1);
        int total_m = 0;
        int total_sessions = 0;
        for (int i = 0; lines[i] != NULL; i++) {
            if (strlen(lines[i]) == 0) continue;
            char **p = g_strsplit(lines[i], "|", 4);
            if (g_strv_length(p) >= 4) {
                total_m += atoi(p[1]);
                total_sessions += atoi(p[3]);
            }
            g_strfreev(p);
        }

        int th = total_m / 60;
        int tm = total_m % 60;
        char sum_buf[128];
        if (th > 0 && tm > 0) {
            snprintf(sum_buf, sizeof(sum_buf), "All Projects: %dh %02dm total (%d sessions)", th, tm, total_sessions);
        } else if (th > 0) {
            snprintf(sum_buf, sizeof(sum_buf), "All Projects: %dh total (%d sessions)", th, total_sessions);
        } else {
            snprintf(sum_buf, sizeof(sum_buf), "All Projects: %dm total (%d sessions)", tm, total_sessions);
        }
        if (stats_proj_summary_lbl != NULL) {
            gtk_label_set_text(GTK_LABEL(stats_proj_summary_lbl), sum_buf);
        }

        for (int i = 0; lines[i] != NULL; i++) {
            if (strlen(lines[i]) == 0) continue;
            char **p = g_strsplit(lines[i], "|", 4);
            if (g_strv_length(p) >= 4) {
                const char *pname = p[0];
                const char *ptimestr = p[2];
                int pcount = atoi(p[3]);

                GtkWidget *card = gtk_button_new();
                gtk_widget_add_css_class(card, "project-summary-card");

                GtkWidget *card_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
                gtk_widget_set_valign(card_box, GTK_ALIGN_CENTER);

                GtkWidget *dot = gtk_label_new("●");
                gtk_widget_add_css_class(dot, "timeline-dot");

                GtkWidget *name_lbl = gtk_label_new(pname);
                gtk_widget_add_css_class(name_lbl, "project-card-name");
                gtk_label_set_xalign(GTK_LABEL(name_lbl), 0.0f);

                char cnt_buf[32];
                snprintf(cnt_buf, sizeof(cnt_buf), "%d session%s", pcount, pcount == 1 ? "" : "s");
                GtkWidget *cnt_lbl = gtk_label_new(cnt_buf);
                gtk_widget_add_css_class(cnt_lbl, "project-card-count");

                GtkWidget *time_lbl = gtk_label_new(ptimestr);
                gtk_widget_add_css_class(time_lbl, "project-card-time");
                gtk_widget_set_hexpand(time_lbl, TRUE);
                gtk_label_set_xalign(GTK_LABEL(time_lbl), 1.0f);

                gtk_box_append(GTK_BOX(card_box), dot);
                gtk_box_append(GTK_BOX(card_box), name_lbl);
                gtk_box_append(GTK_BOX(card_box), cnt_lbl);
                gtk_box_append(GTK_BOX(card_box), time_lbl);
                gtk_button_set_child(GTK_BUTTON(card), card_box);

                char *proj_copy = g_strdup(pname);
                g_signal_connect_data(card, "clicked", G_CALLBACK(on_project_card_clicked), proj_copy, (GClosureNotify)g_free, 0);

                gtk_box_append(GTK_BOX(stats_proj_box), card);
            }
            g_strfreev(p);
        }
        g_strfreev(lines);
    } else {
        // Specific Project selected: filter past sessions for this project
        GtkStringObject *strobj = GTK_STRING_OBJECT(gtk_drop_down_get_selected_item(GTK_DROP_DOWN(stats_proj_dropdown)));
        const char *target_proj = strobj ? gtk_string_object_get_string(strobj) : "";

        char *proj_timestr = g_strdup("0m");
        int proj_count = 0;
        if (cached_project_summaries != NULL) {
            char **lines = g_strsplit(cached_project_summaries, "\n", -1);
            for (int i = 0; lines[i] != NULL; i++) {
                char **p = g_strsplit(lines[i], "|", 4);
                if (g_strv_length(p) >= 4 && strcmp(p[0], target_proj) == 0) {
                    g_free(proj_timestr);
                    proj_timestr = g_strdup(p[2]);
                    proj_count = atoi(p[3]);
                    g_strfreev(p);
                    break;
                }
                g_strfreev(p);
            }
            g_strfreev(lines);
        }

        char sum_buf[128];
        snprintf(sum_buf, sizeof(sum_buf), "%s: %s (%d session%s)", target_proj, proj_timestr, proj_count, proj_count == 1 ? "" : "s");
        g_free(proj_timestr);
        if (stats_proj_summary_lbl != NULL) {
            gtk_label_set_text(GTK_LABEL(stats_proj_summary_lbl), sum_buf);
        }

        int matching = 0;
        if (cached_past_sessions != NULL && strlen(cached_past_sessions) > 0) {
            char **lines = g_strsplit(cached_past_sessions, "\n", -1);
            for (int i = 0; lines[i] != NULL; i++) {
                if (strlen(lines[i]) == 0) continue;
                char **p = g_strsplit(lines[i], "|", 5);
                if (g_strv_length(p) >= 5 && strcmp(p[4], target_proj) == 0) {
                    matching++;
                    const char *date_str = p[0];
                    const char *start_str = p[1];
                    const char *end_str = p[2];
                    const char *dur_str = p[3];

                    GtkWidget *row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
                    gtk_widget_add_css_class(row, "timeline-row");
                    gtk_widget_set_valign(row, GTK_ALIGN_CENTER);

                    GtkWidget *dot = gtk_label_new("●");
                    gtk_widget_add_css_class(dot, "timeline-dot");

                    char dt_buf[64];
                    snprintf(dt_buf, sizeof(dt_buf), "%s  %s – %s", date_str, start_str, end_str);
                    GtkWidget *time_lbl = gtk_label_new(dt_buf);
                    gtk_widget_add_css_class(time_lbl, "timeline-time-range");
                    gtk_label_set_xalign(GTK_LABEL(time_lbl), 0.0f);

                    GtkWidget *dur_lbl = gtk_label_new(dur_str);
                    gtk_widget_add_css_class(dur_lbl, "timeline-dur");
                    gtk_widget_set_hexpand(dur_lbl, TRUE);
                    gtk_label_set_xalign(GTK_LABEL(dur_lbl), 1.0f);

                    gtk_box_append(GTK_BOX(row), dot);
                    gtk_box_append(GTK_BOX(row), time_lbl);
                    gtk_box_append(GTK_BOX(row), dur_lbl);

                    gtk_box_append(GTK_BOX(stats_proj_box), row);
                }
                g_strfreev(p);
            }
            g_strfreev(lines);
        }

        if (matching == 0) {
            GtkWidget *empty_lbl = gtk_label_new("No past sessions found for this project");
            gtk_widget_add_css_class(empty_lbl, "muted-label");
            gtk_widget_set_margin_top(empty_lbl, 28);
            gtk_box_append(GTK_BOX(stats_proj_box), empty_lbl);
        }
    }
}

static void on_project_filter_dropdown_changed(GObject *gobject, GParamSpec *pspec, gpointer user_data) {
    (void)gobject;
    (void)pspec;
    (void)user_data;
    render_projects_view();
}

static gboolean apply_projects_update(gpointer data) {
    ProjectsUpdate *up = (ProjectsUpdate *)data;
    g_free(cached_all_projects);
    cached_all_projects = up->all_projects;

    g_free(cached_project_summaries);
    cached_project_summaries = up->project_summaries;

    g_free(cached_past_sessions);
    cached_past_sessions = up->past_sessions;

    if (stats_proj_dropdown != NULL) {
        char *prev_selected = NULL;
        guint cur_idx = gtk_drop_down_get_selected(GTK_DROP_DOWN(stats_proj_dropdown));
        if (cur_idx > 0) {
            GtkStringObject *strobj = GTK_STRING_OBJECT(gtk_drop_down_get_selected_item(GTK_DROP_DOWN(stats_proj_dropdown)));
            if (strobj != NULL) {
                prev_selected = g_strdup(gtk_string_object_get_string(strobj));
            }
        }

        GPtrArray *items = g_ptr_array_new();
        g_ptr_array_add(items, g_strdup("All Projects"));
        if (cached_all_projects != NULL && strlen(cached_all_projects) > 0) {
            char **projs = g_strsplit(cached_all_projects, "\n", -1);
            for (int i = 0; projs[i] != NULL; i++) {
                if (strlen(projs[i]) > 0) {
                    g_ptr_array_add(items, g_strdup(projs[i]));
                }
            }
            g_strfreev(projs);
        }
        g_ptr_array_add(items, NULL);

        guint restore_idx = 0;
        if (prev_selected != NULL) {
            for (guint i = 1; i < items->len - 1; i++) {
                if (strcmp((char *)items->pdata[i], prev_selected) == 0) {
                    restore_idx = i;
                    break;
                }
            }
            g_free(prev_selected);
        }

        GtkStringList *slist = gtk_string_list_new((const char *const *)items->pdata);
        g_signal_handlers_block_by_func(stats_proj_dropdown, on_project_filter_dropdown_changed, NULL);
        gtk_drop_down_set_model(GTK_DROP_DOWN(stats_proj_dropdown), G_LIST_MODEL(slist));
        gtk_drop_down_set_selected(GTK_DROP_DOWN(stats_proj_dropdown), restore_idx);
        g_signal_handlers_unblock_by_func(stats_proj_dropdown, on_project_filter_dropdown_changed, NULL);

        for (guint i = 0; i < items->len - 1; i++) {
            g_free(items->pdata[i]);
        }
        g_ptr_array_free(items, TRUE);
    }

    render_projects_view();

    g_free(up);
    return G_SOURCE_REMOVE;
}

void pom_gtk_update_projects(const char *all_projects_data, const char *project_summaries_data,
                            const char *past_sessions_data) {
    if (active_update_projects_fn != NULL) {
        active_update_projects_fn(all_projects_data, project_summaries_data, past_sessions_data);
        return;
    }
    ProjectsUpdate *up = g_new0(ProjectsUpdate, 1);
    up->all_projects = g_strdup(all_projects_data);
    up->project_summaries = g_strdup(project_summaries_data);
    up->past_sessions = g_strdup(past_sessions_data);
    g_idle_add(apply_projects_update, up);
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

    GtkWidget *header_row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    gtk_widget_set_hexpand(header_row, TRUE);
    gtk_widget_set_margin_start(header_row, 4);
    gtk_widget_set_margin_end(header_row, 4);
    gtk_widget_set_valign(header_row, GTK_ALIGN_CENTER);

    stats_today_summary_lbl = make_label("0 sessions · 0m focus", "stats-sub-header");
    gtk_widget_set_hexpand(stats_today_summary_lbl, TRUE);
    gtk_label_set_xalign(GTK_LABEL(stats_today_summary_lbl), 0.0f);
    gtk_widget_set_valign(stats_today_summary_lbl, GTK_ALIGN_CENTER);
    gtk_box_append(GTK_BOX(header_row), stats_today_summary_lbl);

    GtkWidget *add_session_btn = gtk_menu_button_new();
    gtk_menu_button_set_icon_name(GTK_MENU_BUTTON(add_session_btn), "list-add-symbolic");
    gtk_widget_add_css_class(add_session_btn, "flat");
    gtk_widget_add_css_class(add_session_btn, "session-add-btn");
    GtkWidget *btn_child = gtk_widget_get_first_child(add_session_btn);
    if (btn_child != NULL) {
        gtk_widget_add_css_class(btn_child, "flat");
        gtk_widget_add_css_class(btn_child, "session-add-btn");
    }
    GtkWidget *btn_img = gtk_button_get_child(GTK_BUTTON(btn_child && GTK_IS_BUTTON(btn_child) ? btn_child : add_session_btn));
    if (btn_img != NULL && GTK_IS_IMAGE(btn_img)) {
        gtk_image_set_pixel_size(GTK_IMAGE(btn_img), 14);
    }
    gtk_widget_set_tooltip_text(add_session_btn, "Add session manually");
    gtk_widget_set_size_request(add_session_btn, 24, 24);
    gtk_widget_set_valign(add_session_btn, GTK_ALIGN_CENTER);

    GtkWidget *popover = create_add_session_popover();
    gtk_menu_button_set_popover(GTK_MENU_BUTTON(add_session_btn), popover);

    gtk_box_append(GTK_BOX(header_row), add_session_btn);
    gtk_box_append(GTK_BOX(box), header_row);

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

static GtkWidget *build_stats_projects_page(void) {
    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);
    gtk_widget_set_vexpand(box, TRUE);
    gtk_widget_set_hexpand(box, TRUE);

    GtkWidget *filter_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    gtk_widget_set_halign(filter_box, GTK_ALIGN_CENTER);

    GtkWidget *filter_lbl = make_label("Project:", "stats-sub-header");
    gtk_box_append(GTK_BOX(filter_box), filter_lbl);

    const char *const init_items[] = {"All Projects", NULL};
    stats_proj_dropdown = gtk_drop_down_new_from_strings(init_items);
    gtk_widget_add_css_class(stats_proj_dropdown, "project-filter-dropdown");
    g_signal_connect(stats_proj_dropdown, "notify::selected", G_CALLBACK(on_project_filter_dropdown_changed), NULL);
    gtk_box_append(GTK_BOX(filter_box), stats_proj_dropdown);

    gtk_box_append(GTK_BOX(box), filter_box);

    stats_proj_summary_lbl = make_label("All Projects: 0m total", "stats-sub-header");
    gtk_widget_set_halign(stats_proj_summary_lbl, GTK_ALIGN_CENTER);
    gtk_box_append(GTK_BOX(box), stats_proj_summary_lbl);

    GtkWidget *scroll = gtk_scrolled_window_new();
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll), GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
    gtk_widget_set_vexpand(scroll, TRUE);
    gtk_widget_set_hexpand(scroll, TRUE);
    gtk_widget_set_size_request(scroll, -1, 185);

    stats_proj_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
    gtk_widget_set_vexpand(stats_proj_box, TRUE);
    gtk_widget_set_hexpand(stats_proj_box, TRUE);
    gtk_widget_add_css_class(stats_proj_box, "timeline-box");

    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scroll), stats_proj_box);
    gtk_box_append(GTK_BOX(box), scroll);

    return box;
}

/* Builds Tab 2: Statistics Page with Today, Weekly, and Projects sub-tabs */
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
    GtkWidget *projects_page = build_stats_projects_page();

    GtkStackPage *p_today = gtk_stack_add_child(GTK_STACK(stats_sub_stack), today_page);
    gtk_stack_page_set_name(p_today, "today");
    gtk_stack_page_set_title(p_today, "Today");

    GtkStackPage *p_week = gtk_stack_add_child(GTK_STACK(stats_sub_stack), week_page);
    gtk_stack_page_set_name(p_week, "week");
    gtk_stack_page_set_title(p_week, "Weekly");

    GtkStackPage *p_proj = gtk_stack_add_child(GTK_STACK(stats_sub_stack), projects_page);
    gtk_stack_page_set_name(p_proj, "projects");
    gtk_stack_page_set_title(p_proj, "Projects");

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

static void save_all_settings(void) {
    if (is_dev_mode) {
        goGTKDevAction((char *)"refresh_state");
    } else {
        goGTKSetSettings(work_duration_val, break_duration_val, long_break_duration_val, total_cycles_val);
    }
}

static void on_duration_entry_changed(GtkEditable *editable, gpointer user_data) {
    const char *text = gtk_editable_get_text(editable);
    if (text == NULL || strlen(text) == 0) return;
    int val = atoi(text);
    int type = (int)(intptr_t)user_data;
    switch (type) {
    case 0:
        if (val >= 1 && val <= 300) { work_duration_val = val; save_all_settings(); }
        break;
    case 1:
        if (val >= 1 && val <= 180) { break_duration_val = val; save_all_settings(); }
        break;
    case 2:
        if (val >= 1 && val <= 180) { long_break_duration_val = val; save_all_settings(); }
        break;
    case 3:
        if (val >= 1 && val <= 12) { total_cycles_val = val; save_all_settings(); }
        break;
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
    int current_val = 25;
    if (type == 0) current_val = work_duration_val;
    else if (type == 1) current_val = break_duration_val;
    else if (type == 2) current_val = long_break_duration_val;
    else if (type == 3) current_val = total_cycles_val;

    const char *text = gtk_editable_get_text(GTK_EDITABLE(entry));
    int val = (text != NULL) ? atoi(text) : 0;
    int max_val = (type == 3) ? 12 : (type == 0 ? 300 : 180);
    if (val < 1 || val > max_val) {
        char buf[16];
        snprintf(buf, sizeof(buf), "%d", current_val);
        gtk_editable_set_text(GTK_EDITABLE(entry), buf);
    }
}

static void focus_setting_entry(int idx);

static void on_duration_entry_activate(GtkEntry *entry, gpointer user_data) {
    int type = (int)(intptr_t)user_data;
    int current_val = 25;
    if (type == 0) current_val = work_duration_val;
    else if (type == 1) current_val = break_duration_val;
    else if (type == 2) current_val = long_break_duration_val;
    else if (type == 3) current_val = total_cycles_val;

    const char *text = gtk_editable_get_text(GTK_EDITABLE(entry));
    int val = (text != NULL) ? atoi(text) : 0;
    int max_val = (type == 3) ? 12 : (type == 0 ? 300 : 180);
    if (val >= 1 && val <= max_val) {
        if (type == 0) work_duration_val = val;
        else if (type == 1) break_duration_val = val;
        else if (type == 2) long_break_duration_val = val;
        else if (type == 3) total_cycles_val = val;
        save_all_settings();
    } else {
        char buf[16];
        snprintf(buf, sizeof(buf), "%d", current_val);
        gtk_editable_set_text(GTK_EDITABLE(entry), buf);
    }

    int next_idx = (type + 1) % 4;
    focus_setting_entry(next_idx);
}

static GtkWidget *build_duration_setting_block(const char *title, int initial_val, int setting_type, const char *unit_str) {
    GtkWidget *block = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
    gtk_widget_set_halign(block, GTK_ALIGN_START);

    GtkWidget *t_lbl = make_label(title, "setting-title");
    gtk_widget_set_halign(t_lbl, GTK_ALIGN_START);
    gtk_label_set_xalign(GTK_LABEL(t_lbl), 0.0f);
    gtk_box_append(GTK_BOX(block), t_lbl);

    GtkWidget *input_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    gtk_widget_set_halign(input_box, GTK_ALIGN_START);
    gtk_widget_set_valign(input_box, GTK_ALIGN_CENTER);

    GtkWidget *entry = gtk_entry_new();
    if (setting_type >= 0 && setting_type < 4) {
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

    GtkWidget *unit_lbl = make_label(unit_str != NULL ? unit_str : "min", "setting-unit");
    gtk_widget_set_halign(unit_lbl, GTK_ALIGN_START);
    gtk_label_set_xalign(GTK_LABEL(unit_lbl), 0.0f);

    gtk_box_append(GTK_BOX(input_box), entry);
    gtk_box_append(GTK_BOX(input_box), unit_lbl);

    gtk_box_append(GTK_BOX(block), input_box);

    return block;
}

static void focus_setting_entry(int idx) {
    if (idx < 0) idx = 0;
    if (idx > 3) idx = 3;
    if (setting_entries[idx] != NULL) {
        gtk_widget_grab_focus(setting_entries[idx]);
        g_idle_add(select_all_idle, setting_entries[idx]);
    }
}

static void switch_setting_entry(int direction) {
    GtkRoot *root = main_stack ? gtk_widget_get_root(main_stack) : NULL;
    GtkWidget *focus = (root && GTK_IS_WINDOW(root)) ? gtk_window_get_focus(GTK_WINDOW(root)) : NULL;

    int current_idx = -1;
    for (int i = 0; i < 4; i++) {
        if (setting_entries[i] != NULL) {
            if (focus == setting_entries[i] || gtk_widget_is_ancestor(focus, setting_entries[i])) {
                current_idx = i;
                break;
            }
        }
    }

    int next_idx;
    if (current_idx == -1) {
        next_idx = (direction > 0) ? 0 : 3;
    } else {
        if (direction > 0) {
            next_idx = (current_idx + 1) % 4;
        } else {
            next_idx = (current_idx - 1 + 4) % 4;
        }
    }
    focus_setting_entry(next_idx);
}

/* Builds Tab 3: Settings Page */
static GtkWidget *build_settings_page(void) {
    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
    gtk_widget_set_vexpand(box, TRUE);
    gtk_widget_set_hexpand(box, TRUE);
    gtk_widget_set_valign(box, GTK_ALIGN_START);
    gtk_widget_set_margin_top(box, 10);

    GtkWidget *title = make_label("Timer Settings", "stats-title");
    gtk_widget_set_halign(title, GTK_ALIGN_START);
    gtk_label_set_xalign(GTK_LABEL(title), 0.0f);
    gtk_box_append(GTK_BOX(box), title);

    gtk_box_append(GTK_BOX(box), gtk_separator_new(GTK_ORIENTATION_HORIZONTAL));

    // 2x2 Layout for setting blocks
    GtkWidget *grid = gtk_grid_new();
    gtk_grid_set_column_spacing(GTK_GRID(grid), 32);
    gtk_grid_set_row_spacing(GTK_GRID(grid), 14);
    gtk_widget_set_halign(grid, GTK_ALIGN_START);
    gtk_widget_set_margin_top(grid, 8);

    GtkWidget *work_block = build_duration_setting_block("Focus Duration", work_duration_val, 0, "min");
    GtkWidget *break_block = build_duration_setting_block("Short Break", break_duration_val, 1, "min");
    GtkWidget *long_break_block = build_duration_setting_block("Long Break", long_break_duration_val, 2, "min");
    GtkWidget *cycles_block = build_duration_setting_block("Circles per Set", total_cycles_val, 3, "circles");

    gtk_grid_attach(GTK_GRID(grid), work_block, 0, 0, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), break_block, 1, 0, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), long_break_block, 0, 1, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), cycles_block, 1, 1, 1, 1);

    gtk_box_append(GTK_BOX(box), grid);

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
GtkWidget *build_pomodoro_card(void) {
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
    static const char *sub_tabs[] = {"today", "week", "projects"};
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

gboolean key_pressed_dev(GtkEventControllerKey *controller, guint keyval,
                        guint keycode, GdkModifierType state, gpointer data) {
    if (active_key_dev_fn != NULL) {
        return active_key_dev_fn(controller, keyval, keycode, state, data);
    }
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
    dev_card_frame = card_frame;

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

void pom_gtk_reload_css(void) {
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
        ".stats-title { font-size: 14px; font-weight: bold; color: #b4befe; }"
        ".stats-sub-switcher { background: #181825; border-radius: 8px; padding: 2px; }"
        ".stats-sub-switcher button { border: none; border-radius: 6px; padding: 2px 14px; min-height: 22px; min-width: 56px; background: transparent; color: #a6adc8; font-size: 11px; font-weight: bold; outline: none; box-shadow: none; }"
        ".stats-sub-switcher button:checked { background: #313244; color: #b4befe; }"
        ".stats-sub-switcher button:hover { color: #cdd6f4; }"
        ".stats-sub-header { color: #a6adc8; font-size: 11px; font-weight: 600; margin-bottom: 2px; }"
        ".timeline-box { padding: 4px 2px; }"
        ".timeline-row { background: #181825; border-radius: 8px; padding: 6px 10px; margin-bottom: 4px; min-height: 28px; }"
        ".timeline-row:hover { background: #232334; }"
        ".timeline-dot { color: #b4befe; font-size: 9px; margin-right: 2px; }"
        ".timeline-time-range { font-family: monospace; font-size: 12px; font-weight: bold; color: #cdd6f4; }"
        "menubutton.timeline-edit-btn, menubutton.timeline-edit-btn > button, menubutton.timeline-edit-btn button, button.timeline-edit-btn { border: none; border-radius: 6px; min-width: 24px; min-height: 24px; padding: 0; background: transparent; background-color: transparent; color: #6c7086; outline: none; box-shadow: none; }"
        "menubutton.timeline-edit-btn:focus, menubutton.timeline-edit-btn > button:focus, menubutton.timeline-edit-btn button:focus { outline: none; box-shadow: none; border: none; }"
        "menubutton.timeline-edit-btn image, menubutton.timeline-edit-btn button image, button.timeline-edit-btn image { -gtk-icon-size: 14px; }"
        "menubutton.timeline-edit-btn:hover, menubutton.timeline-edit-btn > button:hover, menubutton.timeline-edit-btn button:hover, button.timeline-edit-btn:hover { background: rgba(180, 190, 254, 0.18); background-color: rgba(180, 190, 254, 0.18); color: #b4befe; }"
        "menubutton.timeline-edit-btn:checked, menubutton.timeline-edit-btn > button:checked, menubutton.timeline-edit-btn button:checked { background: rgba(180, 190, 254, 0.25); background-color: rgba(180, 190, 254, 0.25); color: #b4befe; }"
        "button.timeline-delete-btn { border: none; border-radius: 6px; min-width: 24px; min-height: 24px; padding: 0; background: transparent; color: #6c7086; outline: none; box-shadow: none; }"
        "button.timeline-delete-btn image { -gtk-icon-size: 14px; }"
        "button.timeline-delete-btn:hover { background: rgba(243, 139, 168, 0.18); color: #f38ba8; }"
        "popover.project-popover { background: #181825; border: 1px solid #45475a; border-radius: 10px; padding: 8px; }"
        "popover.project-popover contents { background: #181825; padding: 6px; }"
        ".project-popover-title { font-size: 11px; font-weight: bold; color: #a6adc8; margin-bottom: 2px; }"
        "button.project-item-btn { border: none; border-radius: 6px; background: transparent; color: #cdd6f4; font-size: 12px; font-weight: 500; padding: 4px 8px; outline: none; box-shadow: none; }"
        "button.project-item-btn:hover { background: #313244; color: #b4befe; }"
        "button.project-clear-btn { border: none; border-radius: 6px; background: transparent; color: #6c7086; font-size: 11px; padding: 3px 8px; outline: none; box-shadow: none; }"
        "button.project-clear-btn:hover { background: rgba(243, 139, 168, 0.15); color: #f38ba8; }"
        "entry.project-new-entry { background: #11111b; color: #cdd6f4; border: 1px solid #313244; border-radius: 6px; font-size: 11px; min-height: 24px; padding: 2px 6px; box-shadow: none; outline: none; }"
        "entry.project-new-entry:focus-within { border-color: #b4befe; }"
        "entry.project-new-entry text { color: #cdd6f4; background: transparent; min-width: 0; min-height: 0; padding: 0; }"
        "button.project-add-btn { border: none; border-radius: 6px; min-width: 24px; min-height: 24px; padding: 0; background: #313244; color: #b4befe; outline: none; box-shadow: none; }"
        "button.project-add-btn image { -gtk-icon-size: 12px; }"
        "button.project-add-btn:hover { background: #45475a; color: #cdd6f4; }"
        "dropdown.project-filter-dropdown, .project-filter-dropdown button { background: #181825; color: #cdd6f4; border: 1px solid #313244; border-radius: 6px; font-size: 11px; font-weight: bold; min-height: 24px; padding: 2px 8px; }"
        "dropdown.project-filter-dropdown:hover, .project-filter-dropdown button:hover { border-color: #45475a; color: #b4befe; }"
        ".project-summary-card { background: #181825; border-radius: 8px; padding: 8px 12px; margin-bottom: 4px; min-height: 32px; border: 1px solid transparent; }"
        ".project-summary-card:hover { background: #232334; border-color: #313244; }"
        ".project-card-name { font-size: 12px; font-weight: bold; color: #cba6f7; }"
        ".project-card-time { font-family: monospace; font-size: 12px; font-weight: bold; color: #a6e3a1; }"
        ".project-card-count { font-size: 10px; font-weight: 500; color: #6c7086; }"
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
        "entry.setting-entry selection, entry.setting-entry selection:focus, entry.setting-entry text selection, entry.setting-entry text selection:focus { background-color: transparent; color: #cdd6f4; }"
        "entry.setting-entry text { color: #cdd6f4; background: transparent; min-width: 0; min-height: 0; padding: 0; }"
        ".setting-unit { color: #a6adc8; font-size: 12px; font-weight: 500; }"
        "button.session-add-btn { border: none; border-radius: 6px; min-width: 24px; min-height: 24px; padding: 0; background: #313244; color: #b4befe; outline: none; box-shadow: none; }"
        "button.session-add-btn:hover { background: #45475a; color: #cdd6f4; }"
        "button.session-add-btn image { -gtk-icon-size: 14px; }"
        "popover.session-popover { background: #181825; border: 1px solid #45475a; border-radius: 10px; padding: 8px; }"
        "popover.session-popover contents { background: #181825; padding: 6px; }"
        "entry.session-entry { background: #11111b; color: #cdd6f4; border: 1px solid #313244; border-radius: 6px; font-size: 12px; min-height: 26px; padding: 2px 6px; box-shadow: none; outline: none; }"
        "entry.session-entry:focus-within { border-color: #b4befe; }"
        "entry.session-entry text { color: #cdd6f4; background: transparent; min-width: 0; min-height: 0; padding: 0; }"
        "button.session-submit-btn { border: none; border-radius: 6px; background: #b4befe; color: #11111b; font-size: 12px; font-weight: bold; min-height: 28px; padding: 4px 10px; }"
        "button.session-submit-btn:hover { background: #cdd6f4; color: #11111b; }"
        "button.project-chip-btn { border: 1px solid #313244; border-radius: 10px; background: #181825; color: #a6adc8; font-size: 10px; font-weight: 500; padding: 2px 6px; min-height: 18px; outline: none; box-shadow: none; }"
        "button.project-chip-btn:hover { background: #313244; color: #b4befe; border-color: #45475a; }"
        ".tab-switcher { background: transparent; border: none; padding: 0; }"
        ".tab-switcher button { border: none; border-bottom: 2px solid transparent; border-radius: 0; padding: 4px 12px; min-height: 28px; min-width: 38px; background: transparent; color: #6c7086; outline: none; box-shadow: none; }"
        ".tab-switcher button:hover { color: #a6adc8; }"
        ".tab-switcher button:checked { background: transparent; color: #cdd6f4; border-bottom: 2px solid #b4befe; }"
        ".dev-error-box { background: rgba(243, 139, 168, 0.15); border: 1px solid #f38ba8; border-radius: 8px; padding: 6px 14px; margin: 0 20px; }"
        ".dev-error-text { color: #f38ba8; font-size: 12px; font-weight: bold; }"
        "window.pomodoro-toast-window { background: #1e1e2e; color: #cdd6f4; border: 1.5px solid #45475a; border-radius: 12px; }"
        ".toast-content { padding: 12px 14px; }"
        ".toast-icon { font-size: 24px; margin-right: 4px; }"
        ".toast-title { font-size: 13px; font-weight: bold; color: #b4befe; }"
        ".toast-subtitle { font-size: 11px; color: #cdd6f4; }"
        "button.toast-close-btn { border: none; border-radius: 12px; min-width: 24px; min-height: 24px; padding: 0; background: transparent; color: #6c7086; outline: none; box-shadow: none; }"
        "button.toast-close-btn:hover { background: #313244; color: #cdd6f4; }";

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

static void on_startup(GApplication *app, gpointer data) {
    (void)app;
    (void)data;
    pom_gtk_reload_css();
}

static gboolean show_compile_error_idle(gpointer data) {
    char *err = (char *)data;
    if (dev_error_label != NULL && dev_error_box != NULL) {
        char formatted[600];
        snprintf(formatted, sizeof(formatted), "⚠️ %s", err ? err : "Build Error");
        gtk_label_set_text(GTK_LABEL(dev_error_label), formatted);
        gtk_widget_set_visible(dev_error_box, TRUE);
    }
    g_free(err);
    return G_SOURCE_REMOVE;
}

void pom_gtk_dev_show_compile_error(const char *error_msg) {
    g_idle_add(show_compile_error_idle, g_strdup(error_msg));
}

static gboolean clear_compile_error_idle(gpointer data) {
    (void)data;
    if (dev_error_box != NULL) {
        gtk_widget_set_visible(dev_error_box, FALSE);
    }
    return G_SOURCE_REMOVE;
}

void pom_gtk_dev_clear_compile_error(void) {
    g_idle_add(clear_compile_error_idle, NULL);
}

static gboolean apply_module_reload_idle(gpointer data) {
    char *so_path = (char *)data;
    void *h = dlopen(so_path, RTLD_NOW | RTLD_GLOBAL);
    if (!h) {
        const char *err = dlerror();
        pom_gtk_dev_show_compile_error(err ? err : "Failed to load module");
        g_free(so_path);
        return G_SOURCE_REMOVE;
    }

    DevBuildCardFn build_card = (DevBuildCardFn)dlsym(h, "build_pomodoro_card");
    if (!build_card) {
        pom_gtk_dev_show_compile_error("Missing build_pomodoro_card in module");
        dlclose(h);
        g_free(so_path);
        return G_SOURCE_REMOVE;
    }

    if (dev_error_box != NULL) {
        gtk_widget_set_visible(dev_error_box, FALSE);
    }

    DevReloadCssFn reload_css = (DevReloadCssFn)dlsym(h, "pom_gtk_reload_css");
    if (reload_css) {
        reload_css();
    }

    if (dev_card_frame != NULL) {
        GtkWidget *child = gtk_widget_get_first_child(dev_card_frame);
        while (child != NULL) {
            GtkWidget *next = gtk_widget_get_next_sibling(child);
            gtk_box_remove(GTK_BOX(dev_card_frame), child);
            child = next;
        }

        GtkWidget *new_card = build_card();
        if (new_card != NULL) {
            gtk_box_append(GTK_BOX(dev_card_frame), new_card);
        }
    }

    active_update_fn = (DevUpdateFn)dlsym(h, "pom_gtk_update");
    active_update_stats_fn = (DevUpdateStatsFn)dlsym(h, "pom_gtk_update_stats");
    active_update_projects_fn = (DevUpdateProjectsFn)dlsym(h, "pom_gtk_update_projects");
    active_key_dev_fn = (DevKeyFn)dlsym(h, "key_pressed_dev");

    current_dev_module_handle = h;
    g_free(so_path);

    goGTKDevAction("refresh_state");

    return G_SOURCE_REMOVE;
}

void pom_gtk_dev_load_module(const char *so_path) {
    g_idle_add(apply_module_reload_idle, g_strdup(so_path));
}

static gboolean quit_application(gpointer data) {
    if (application != NULL) {
        g_application_quit(G_APPLICATION(application));
    }
    main_stack = NULL;
    stats_sub_stack = NULL;
    stats_today_box = NULL;
    stats_week_box = NULL;
    stats_proj_box = NULL;
    stats_today_summary_lbl = NULL;
    stats_week_summary_lbl = NULL;
    stats_proj_summary_lbl = NULL;
    stats_proj_dropdown = NULL;
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
    stats_proj_box = NULL;
    stats_today_summary_lbl = NULL;
    stats_week_summary_lbl = NULL;
    stats_proj_summary_lbl = NULL;
    stats_proj_dropdown = NULL;
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
    stats_proj_box = NULL;
    stats_today_summary_lbl = NULL;
    stats_week_summary_lbl = NULL;
    stats_proj_summary_lbl = NULL;
    stats_proj_dropdown = NULL;
    return status;
}

int pom_gtk_dev_run(void) {
    is_dev_mode = 1;
    main_stack = NULL;
    stats_sub_stack = NULL;
    stats_today_box = NULL;
    stats_week_box = NULL;
    stats_proj_box = NULL;
    stats_today_summary_lbl = NULL;
    stats_week_summary_lbl = NULL;
    stats_proj_summary_lbl = NULL;
    stats_proj_dropdown = NULL;
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

static char *toast_title_val = NULL;
static char *toast_message_val = NULL;

static gboolean toast_auto_close_cb(gpointer user_data) {
    GtkWindow *win = GTK_WINDOW(user_data);
    if (win != NULL && GTK_IS_WINDOW(win)) {
        gtk_window_destroy(win);
    }
    return G_SOURCE_REMOVE;
}

static void toast_close_clicked(GtkButton *btn, gpointer user_data) {
    (void)btn;
    GtkWindow *win = GTK_WINDOW(user_data);
    if (win != NULL && GTK_IS_WINDOW(win)) {
        gtk_window_destroy(win);
    }
}

static gboolean toast_key_pressed(GtkEventControllerKey *controller, guint keyval,
                                   guint keycode, GdkModifierType state, gpointer data) {
    (void)controller;
    (void)keyval;
    (void)keycode;
    (void)state;
    (void)data;
    GtkWidget *win = gtk_event_controller_get_widget(GTK_EVENT_CONTROLLER(controller));
    if (win != NULL && GTK_IS_WINDOW(win)) {
        gtk_window_destroy(GTK_WINDOW(win));
        return TRUE;
    }
    return FALSE;
}

static void activate_toast(GtkApplication *app, gpointer user_data) {
    (void)user_data;
    GtkWidget *window = gtk_application_window_new(app);
    gtk_window_set_title(GTK_WINDOW(window), "Pomodoro Alert");
    gtk_window_set_default_size(GTK_WINDOW(window), 320, 90);
    gtk_window_set_resizable(GTK_WINDOW(window), FALSE);
    gtk_window_set_decorated(GTK_WINDOW(window), FALSE);
    gtk_widget_set_opacity(window, 0.95);
    gtk_widget_add_css_class(window, "pomodoro-toast-window");

    GtkWidget *toast_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
    gtk_widget_add_css_class(toast_box, "toast-content");
    gtk_widget_set_valign(toast_box, GTK_ALIGN_CENTER);
    gtk_widget_set_halign(toast_box, GTK_ALIGN_FILL);

    const char *icon_str = "🍅";
    if (toast_title_val != NULL) {
        if (strstr(toast_title_val, "Break") || strstr(toast_title_val, "break")) {
            icon_str = "☕";
        }
        if (strstr(toast_title_val, "All") || strstr(toast_title_val, "🎉")) {
            icon_str = "🎉";
        }
    }

    GtkWidget *icon_lbl = gtk_label_new(icon_str);
    gtk_widget_add_css_class(icon_lbl, "toast-icon");
    gtk_widget_set_valign(icon_lbl, GTK_ALIGN_CENTER);
    gtk_box_append(GTK_BOX(toast_box), icon_lbl);

    GtkWidget *text_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);
    gtk_widget_set_hexpand(text_box, TRUE);
    gtk_widget_set_valign(text_box, GTK_ALIGN_CENTER);

    GtkWidget *t_lbl = gtk_label_new(toast_title_val ? toast_title_val : "Pomodoro Alert");
    gtk_widget_add_css_class(t_lbl, "toast-title");
    gtk_label_set_xalign(GTK_LABEL(t_lbl), 0.0f);

    GtkWidget *m_lbl = gtk_label_new(toast_message_val ? toast_message_val : "");
    gtk_widget_add_css_class(m_lbl, "toast-subtitle");
    gtk_label_set_xalign(GTK_LABEL(m_lbl), 0.0f);
    gtk_label_set_wrap(GTK_LABEL(m_lbl), TRUE);
    gtk_label_set_max_width_chars(GTK_LABEL(m_lbl), 35);

    gtk_box_append(GTK_BOX(text_box), t_lbl);
    gtk_box_append(GTK_BOX(text_box), m_lbl);
    gtk_box_append(GTK_BOX(toast_box), text_box);

    GtkWidget *close_btn = gtk_button_new_from_icon_name("window-close-symbolic");
    gtk_widget_add_css_class(close_btn, "toast-close-btn");
    gtk_widget_set_tooltip_text(close_btn, "Dismiss");
    gtk_widget_set_valign(close_btn, GTK_ALIGN_CENTER);
    g_signal_connect(close_btn, "clicked", G_CALLBACK(toast_close_clicked), window);
    gtk_box_append(GTK_BOX(toast_box), close_btn);

    gtk_window_set_child(GTK_WINDOW(window), toast_box);

    GtkEventController *keys = gtk_event_controller_key_new();
    g_signal_connect(keys, "key-pressed", G_CALLBACK(toast_key_pressed), NULL);
    gtk_widget_add_controller(window, keys);

    g_timeout_add_seconds(5, toast_auto_close_cb, window);

    gtk_window_present(GTK_WINDOW(window));
}

int pom_gtk_run_toast(const char *title, const char *message) {
    toast_title_val = (char *)title;
    toast_message_val = (char *)message;

    GtkApplication *app = gtk_application_new("io.github.waybarpomodoro.gtktoast", G_APPLICATION_DEFAULT_FLAGS);
    g_signal_connect(app, "startup", G_CALLBACK(on_startup), NULL);
    g_signal_connect(app, "activate", G_CALLBACK(activate_toast), NULL);

    int status = g_application_run(G_APPLICATION(app), 0, NULL);
    g_object_unref(app);
    toast_title_val = NULL;
    toast_message_val = NULL;
    return status;
}
