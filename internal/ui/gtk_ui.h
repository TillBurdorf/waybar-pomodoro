#ifndef WAYBAR_POMODORO_GTK_UI_H
#define WAYBAR_POMODORO_GTK_UI_H

int pom_gtk_run(void);
int pom_gtk_dev_run(void);
void pom_gtk_update(const char *mode, const char *timer, const char *status,
                    const char *progress, const char *cycle,
                    const char *stats, const char *history, double fraction);
void pom_gtk_update_stats(const char *today_summary, const char *today_blocks_data,
                          const char *week_summary, const char *week_days_data);
void pom_gtk_quit_async(void);
void pom_gtk_set_initial_durations(int work_min, int break_min);
void pom_gtk_dev_show_fallback_error(const char *error_msg);

extern void goGTKCommand(char *command);
extern void goGTKDevAction(char *action);
extern void goGTKSetDurations(int work_min, int break_min);

#endif
