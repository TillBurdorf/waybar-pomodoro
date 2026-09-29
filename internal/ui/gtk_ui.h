#ifndef WAYBAR_POMODORO_GTK_UI_H
#define WAYBAR_POMODORO_GTK_UI_H

int pom_gtk_run(void);
int pom_gtk_dev_run(void);
void pom_gtk_update(const char *mode, const char *timer, const char *status,
                    const char *progress, const char *cycle,
                    const char *stats, const char *history, double fraction);
void pom_gtk_quit_async(void);

extern void goGTKCommand(char *command);
extern void goGTKDevAction(char *action);

#endif
