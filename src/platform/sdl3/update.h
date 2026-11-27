/* SYSTEM > SETTINGS > UPDATES (Android only, docs/ANDROID.md): checks the
 * "android-latest" GitHub release for a newer build and installs it. The
 * work is done by the app's Java side (android/.../Updater.java), reached
 * over JNI; this file draws the page and passes the buttons on. */
#ifndef UPDATE_H
#define UPDATE_H

#include "garage.h"

/* Keep in step with Updater.java. */
enum {
    UPDATE_IDLE, UPDATE_CHECKING, UPDATE_UP_TO_DATE, UPDATE_AVAILABLE, UPDATE_DOWNLOADING,
    UPDATE_CONFIRM, UPDATE_FAILED, UPDATE_NEEDS_PERMISSION, UPDATE_NEWER_HERE
};

/* 1 where the page exists (the Android app). */
int  update_supported(void);

typedef struct update_ui {
    int cur;
} update_ui;

/* Opening the page checks for an update straight away. */
void update_open(update_ui *ui);
/* Returns 1 when the page is left. */
int  update_input(update_ui *ui, garage_action a);
void update_draw(update_ui *ui, float hud_w);

#endif
