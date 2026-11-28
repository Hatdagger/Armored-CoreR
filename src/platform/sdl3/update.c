#include "update.h"

#include <SDL3/SDL.h>

#include <string.h>

#include "../../render/acr_render.h"

#define WHITE 220, 230, 235
#define DIM 120, 135, 145
#define HILITE 255, 220, 120
#define WARN 255, 120, 100

#ifdef SDL_PLATFORM_ANDROID
#define ANDROID_UI 1
#else
#define ANDROID_UI 0
#endif

enum { R_CHECK, R_INSTALL, R_BACK, R_COUNT };

#ifdef SDL_PLATFORM_ANDROID
#include <jni.h>

/* io.github.hatdagger.armoredcorer.Updater, looked up once. */
static jclass g_cls;

static JNIEnv *jenv(void) {
    JNIEnv *env = (JNIEnv *)SDL_GetAndroidJNIEnv();
    if (!env) return NULL;
    if (!g_cls) {
        jclass c = (*env)->FindClass(env, "io/github/hatdagger/armoredcorer/Updater");
        if (!c) { (*env)->ExceptionClear(env); return NULL; }
        g_cls = (jclass)(*env)->NewGlobalRef(env, c);
        (*env)->DeleteLocalRef(env, c);
    }
    return env;
}

static void clear(JNIEnv *env) {
    if ((*env)->ExceptionCheck(env)) { (*env)->ExceptionDescribe(env); (*env)->ExceptionClear(env); }
}

/* A static method taking the activity (sig "(Landroid/app/Activity;)V" or "...I"). */
static int call_activity(const char *name, const char *sig) {
    JNIEnv *env = jenv();
    if (!env) return 0;
    jmethodID m = (*env)->GetStaticMethodID(env, g_cls, name, sig);
    if (!m) { clear(env); return 0; }
    jobject act = (jobject)SDL_GetAndroidActivity();
    int r = 0;
    if (sig[SDL_strlen(sig) - 1] == 'I') r = (*env)->CallStaticIntMethod(env, g_cls, m, act);
    else (*env)->CallStaticVoidMethod(env, g_cls, m, act);
    clear(env);
    if (act) (*env)->DeleteLocalRef(env, act);
    return r;
}

static int get_int(const char *name) {
    JNIEnv *env = jenv();
    if (!env) return 0;
    jmethodID m = (*env)->GetStaticMethodID(env, g_cls, name, "()I");
    if (!m) { clear(env); return 0; }
    const int r = (*env)->CallStaticIntMethod(env, g_cls, m);
    clear(env);
    return r;
}

/* A String, upper-cased for the HUD font. */
static void get_text(JNIEnv *env, jstring s, char *out, size_t size) {
    out[0] = 0;
    if (!s) return;
    const char *c = (*env)->GetStringUTFChars(env, s, NULL);
    if (c) {
        SDL_strlcpy(out, c, size);
        (*env)->ReleaseStringUTFChars(env, s, c);
    }
    (*env)->DeleteLocalRef(env, s);
    for (char *p = out; *p; ++p) *p = (char)SDL_toupper((unsigned char)*p);
}

static void get_string(const char *name, char *out, size_t size) {
    out[0] = 0;
    JNIEnv *env = jenv();
    if (!env) return;
    jmethodID m = (*env)->GetStaticMethodID(env, g_cls, name, "()Ljava/lang/String;");
    if (!m) { clear(env); return; }
    jstring s = (jstring)(*env)->CallStaticObjectMethod(env, g_cls, m);
    clear(env);
    get_text(env, s, out, size);
}

static void local_name(char *out, size_t size) {
    out[0] = 0;
    JNIEnv *env = jenv();
    if (!env) return;
    jmethodID m = (*env)->GetStaticMethodID(env, g_cls, "localName", "(Landroid/app/Activity;)Ljava/lang/String;");
    if (!m) { clear(env); return; }
    jobject act = (jobject)SDL_GetAndroidActivity();
    jstring s = (jstring)(*env)->CallStaticObjectMethod(env, g_cls, m, act);
    clear(env);
    if (act) (*env)->DeleteLocalRef(env, act);
    get_text(env, s, out, size);
}

int update_supported(void) { return 1; }
static void do_check(void) { call_activity("check", "(Landroid/app/Activity;)V"); }
static void do_install(void) { call_activity("install", "(Landroid/app/Activity;)V"); }
static int local_build(void) { return call_activity("localBuild", "(Landroid/app/Activity;)I"); }

#else   /* Windows / Linux: the "desktop-latest" GitHub release */

/* The PC build fetches with the system's curl and unpacks with tar (both
 * ship with Windows 10+ and every desktop Linux). The new files go to a
 * staging folder; RESTART runs a small script that waits for the game to
 * close, copies them over this install and starts it again. Saves live in
 * the pref folder, so they are never touched. */

#ifdef SDL_PLATFORM_WINDOWS
#include <windows.h>
#define ACR_ARCHIVE "armored-corer-windows-x64.zip"
#define ACR_SIZE_LINE 3
static unsigned long my_pid(void) { return (unsigned long)GetCurrentProcessId(); }
#else
#include <unistd.h>
#define ACR_ARCHIVE "armored-corer-linux-x64.tar.gz"
#define ACR_SIZE_LINE 4
static unsigned long my_pid(void) { return (unsigned long)getpid(); }
#endif

#ifndef ACR_BUILD_NUMBER
#define ACR_BUILD_NUMBER 0
#endif
#define ACR_URL "https://github.com/Hatdagger/Armored-CoreR/releases/download/desktop-latest/"

static SDL_AtomicInt g_state, g_remote, g_progress;
static SDL_AtomicInt g_busy;
static char g_msg[160], g_remote_text[64];
static Sint64 g_remote_size;
static char g_dir[1024];   /* pref/update/ */

static const char *dir(void) {
    if (!g_dir[0]) {
        char *pref = SDL_GetPrefPath("Hatdagger", "Armored-CoreR");
        SDL_snprintf(g_dir, sizeof g_dir, "%supdate%s", pref ? pref : "",
#ifdef SDL_PLATFORM_WINDOWS
                     "\\");
#else
                     "/");
#endif
        SDL_free(pref);
        SDL_CreateDirectory(g_dir);
    }
    return g_dir;
}

static void fail(const char *m) {
    SDL_strlcpy(g_msg, m, sizeof g_msg);
    for (char *p = g_msg; *p; ++p) *p = (char)SDL_toupper((unsigned char)*p);
    SDL_SetAtomicInt(&g_state, UPDATE_FAILED);
}

/* Runs a program and waits; 1 when it exited with 0. */
static int run(const char *const *args) {
    SDL_Process *p = SDL_CreateProcess(args, false);
    if (!p) return 0;
    int code = -1;
    SDL_WaitProcess(p, true, &code);
    SDL_DestroyProcess(p);
    return code == 0;
}

static int fetch(const char *file, const char *to) {
    char url[256];
    SDL_snprintf(url, sizeof url, "%s%s", ACR_URL, file);
    const char *args[] = { "curl", "-fsSL", "--retry", "2", "-o", to, url, NULL };
    return run(args);
}

static int check_thread(void *u) {
    (void)u;
    char path[1100];
    SDL_snprintf(path, sizeof path, "%sversion.txt", dir());
    SDL_RemovePath(path);
    if (!fetch("version.txt", path)) { fail("Could not reach GitHub (is curl installed?)"); goto done; }
    size_t n = 0;
    char *txt = (char *)SDL_LoadFile(path, &n);
    if (!txt) { fail("Could not read the version file"); goto done; }
    /* build, commit, date, windows size, linux size */
    char *lines[6] = {0};
    int count = 0;
    for (char *t = txt, *save = NULL; count < 6 && (t = SDL_strtok_r(count ? NULL : t, "\r\n", &save)); ++count)
        lines[count] = t;
    const int remote = lines[0] ? SDL_atoi(lines[0]) : 0;
    g_remote_size = lines[ACR_SIZE_LINE] ? (Sint64)SDL_strtoll(lines[ACR_SIZE_LINE], NULL, 10) : 0;
    SDL_snprintf(g_remote_text, sizeof g_remote_text, "%s %s", lines[1] ? lines[1] : "", lines[2] ? lines[2] : "");
    for (char *p = g_remote_text; *p; ++p) *p = (char)SDL_toupper((unsigned char)*p);
    SDL_free(txt);
    if (remote <= 0) { fail("No desktop build has been published yet"); goto done; }
    SDL_SetAtomicInt(&g_remote, remote);
    SDL_SetAtomicInt(&g_state, remote > ACR_BUILD_NUMBER ? UPDATE_AVAILABLE
                             : remote < ACR_BUILD_NUMBER ? UPDATE_NEWER_HERE : UPDATE_UP_TO_DATE);
done:
    SDL_SetAtomicInt(&g_busy, 0);
    return 0;
}

static int install_thread(void *u) {
    (void)u;
    char archive[1100], staged[1100];
    SDL_snprintf(archive, sizeof archive, "%s%s", dir(), ACR_ARCHIVE);
    SDL_snprintf(staged, sizeof staged, "%snew", dir());
    SDL_RemovePath(archive);
    SDL_SetAtomicInt(&g_progress, 0);

    char url[256];
    SDL_snprintf(url, sizeof url, "%s%s", ACR_URL, ACR_ARCHIVE);
    const char *dl[] = { "curl", "-fsSL", "--retry", "2", "-o", archive, url, NULL };
    SDL_Process *p = SDL_CreateProcess(dl, false);
    if (!p) { fail("Could not start curl"); goto done; }
    int code = -1;
    while (!SDL_WaitProcess(p, false, &code)) {
        SDL_PathInfo info;
        if (g_remote_size > 0 && SDL_GetPathInfo(archive, &info))
            SDL_SetAtomicInt(&g_progress, (int)SDL_min(99, info.size * 100 / g_remote_size));
        SDL_Delay(200);
    }
    SDL_DestroyProcess(p);
    if (code != 0) { fail("Download failed"); goto done; }
    SDL_SetAtomicInt(&g_progress, 100);

    /* a clean staging folder, then unpack */
#ifdef SDL_PLATFORM_WINDOWS
    const char *rm[] = { "cmd.exe", "/c", "rmdir", "/s", "/q", staged, NULL };
#else
    const char *rm[] = { "rm", "-rf", staged, NULL };
#endif
    run(rm);
    SDL_CreateDirectory(staged);
    const char *untar[] = { "tar", "-xf", archive, "-C", staged, NULL };
    if (!run(untar)) { fail("Could not unpack the download"); goto done; }
    SDL_SetAtomicInt(&g_state, UPDATE_CONFIRM);
done:
    SDL_SetAtomicInt(&g_busy, 0);
    return 0;
}

static int start(SDL_ThreadFunction fn, int state) {
    if (SDL_GetAtomicInt(&g_busy)) return 0;
    SDL_SetAtomicInt(&g_busy, 1);
    g_msg[0] = 0;
    SDL_SetAtomicInt(&g_state, state);
    SDL_Thread *t = SDL_CreateThread(fn, "acr-update", NULL);
    if (!t) { SDL_SetAtomicInt(&g_busy, 0); fail("Could not start the updater"); return 0; }
    SDL_DetachThread(t);
    return 1;
}

/* Writes the swap script, starts it and closes the game. */
static void restart(void) {
    const char *base = SDL_GetBasePath();
    char script[1100], staged[1100];
    SDL_snprintf(staged, sizeof staged, "%snew", dir());
#ifdef SDL_PLATFORM_WINDOWS
    SDL_snprintf(script, sizeof script, "%sapply.bat", dir());
    char body[4096];
    SDL_snprintf(body, sizeof body,
        "@echo off\r\n"
        ":wait\r\n"
        "tasklist /FI \"PID eq %lu\" 2>nul | find \"%lu\" >nul && (timeout /t 1 /nobreak >nul & goto wait)\r\n"
        "xcopy /E /Y /Q /I \"%s\\*\" \"%s.\" >nul\r\n"
        "start \"\" \"%sarmored-corer.exe\"\r\n",
        my_pid(), my_pid(), staged, base, base);
    if (!SDL_SaveFile(script, body, SDL_strlen(body))) { fail("Could not write the update script"); return; }
    const char *args[] = { "cmd.exe", "/c", script, NULL };
#else
    SDL_snprintf(script, sizeof script, "%sapply.sh", dir());
    char body[4096];
    SDL_snprintf(body, sizeof body,
        "#!/bin/sh\n"
        "while kill -0 %lu 2>/dev/null; do sleep 1; done\n"
        "cp -rf \"%s\"/. \"%s\"\n"
        "chmod +x \"%sarmored-corer\" \"%sacr-import\" 2>/dev/null\n"
        "cd \"%s\" && exec ./armored-corer\n",
        my_pid(), staged, base, base, base, base);
    if (!SDL_SaveFile(script, body, SDL_strlen(body))) { fail("Could not write the update script"); return; }
    const char *args[] = { "sh", script, NULL };
#endif
    SDL_Process *p = SDL_CreateProcess(args, false);
    if (!p) { fail("Could not start the update script"); return; }
    /* not destroyed: the script outlives the game */
    SDL_Event q;
    SDL_zero(q);
    q.type = SDL_EVENT_QUIT;
    SDL_PushEvent(&q);
}

int update_supported(void) { return 1; }
static void do_check(void) { start(check_thread, UPDATE_CHECKING); }
static void do_install(void) {
    if (SDL_GetAtomicInt(&g_state) == UPDATE_CONFIRM) restart();
    else start(install_thread, UPDATE_DOWNLOADING);
}
static int local_build(void) { return ACR_BUILD_NUMBER; }
static int get_int(const char *name) {
    if (SDL_strcmp(name, "state") == 0) return SDL_GetAtomicInt(&g_state);
    if (SDL_strcmp(name, "remoteBuild") == 0) return SDL_GetAtomicInt(&g_remote);
    if (SDL_strcmp(name, "progress") == 0) return SDL_GetAtomicInt(&g_progress);
    return 0;
}
static void get_string(const char *name, char *out, size_t size) {
    out[0] = 0;
    if (SDL_GetAtomicInt(&g_busy)) return;   /* written by the worker */
    if (SDL_strcmp(name, "message") == 0) SDL_strlcpy(out, g_msg, size);
    else if (SDL_strcmp(name, "remoteText") == 0) SDL_strlcpy(out, g_remote_text, size);
}
static void local_name(char *out, size_t size) {
    if (ACR_BUILD_NUMBER > 0) SDL_snprintf(out, size, "0.1-TEST.%d (PC)", ACR_BUILD_NUMBER);
    else SDL_strlcpy(out, "LOCAL PC BUILD", size);
}

#endif

void update_open(update_ui *ui) {
    memset(ui, 0, sizeof *ui);
    do_check();
}

static int can_install(int state) {
    return state == UPDATE_AVAILABLE || state == UPDATE_NEEDS_PERMISSION ||
#ifndef SDL_PLATFORM_ANDROID
           state == UPDATE_CONFIRM ||
#endif
           (state == UPDATE_FAILED && get_int("remoteBuild") > local_build());
}

int update_input(update_ui *ui, garage_action a) {
    if (a == GA_NONE) return 0;
    if (a == GA_UP) ui->cur = (ui->cur + R_COUNT - 1) % R_COUNT;
    if (a == GA_DOWN) ui->cur = (ui->cur + 1) % R_COUNT;
    if (a == GA_CANCEL) return 1;
    if (a != GA_OK) return 0;
    const int state = get_int("state");
    switch (ui->cur) {
    case R_CHECK:
        if (state != UPDATE_CHECKING && state != UPDATE_DOWNLOADING) do_check();
        return 0;
    case R_INSTALL:
        if (can_install(state)) do_install();
        return 0;
    default: return 1;
    }
}

void update_draw(update_ui *ui, float hud_w) {
    (void)hud_w;
    const float lx = 24, w = 460;
    const int state = get_int("state"), remote = get_int("remoteBuild");
    char here[64], line[96], msg[160], remote_text[64];
    local_name(here, sizeof here);
    get_string("remoteText", remote_text, sizeof remote_text);
    get_string("message", msg, sizeof msg);

    acr_hud_text(lx, 16, "SETTINGS: UPDATES", HILITE);
    acr_hud_rect(lx, 40, w, 210, 10, 14, 20, 210, 1);
    acr_hud_rect(lx, 40, w, 210, 70, 90, 100, 255, 0);

    SDL_snprintf(line, sizeof line, "THIS BUILD    %s", here);
    acr_hud_text(lx + 16, 56, line, WHITE);
    if (remote > 0) {
        SDL_snprintf(line, sizeof line, "LATEST        0.1-TEST.%d %s", remote, remote_text);
        acr_hud_text(lx + 16, 74, line, WHITE);
    }

    /* what is going on */
    const char *status = "";
    switch (state) {
    case UPDATE_CHECKING: status = "CHECKING GITHUB..."; break;
    case UPDATE_UP_TO_DATE: status = "UP TO DATE."; break;
    case UPDATE_AVAILABLE: status = "A NEW BUILD IS READY."; break;
    case UPDATE_DOWNLOADING:
        SDL_snprintf(line, sizeof line, "DOWNLOADING... %d%%", get_int("progress"));
        status = line;
        break;
#ifdef SDL_PLATFORM_ANDROID
    case UPDATE_CONFIRM: status = "CONFIRM ON ANDROID'S INSTALL SCREEN."; break;
#else
    case UPDATE_CONFIRM: status = "DOWNLOADED. CHOOSE RESTART TO FINISH."; break;
#endif
    case UPDATE_NEEDS_PERMISSION: status = "ALLOW \"INSTALL UNKNOWN APPS\", COME BACK, PRESS A AGAIN."; break;
    case UPDATE_NEWER_HERE: status = "THIS BUILD IS NEWER THAN THE LATEST RELEASE."; break;
    case UPDATE_FAILED: status = ""; break;
    default: break;
    }
    const int bad = state == UPDATE_FAILED;
    if (status[0]) acr_hud_text(lx + 16, 98, status, HILITE);
    if (state == UPDATE_DOWNLOADING) {
        const int p = SDL_clamp(get_int("progress"), 0, 100);
        acr_hud_rect(lx + 16, 114, 300, 8, 50, 60, 70, 255, 1);
        acr_hud_rect(lx + 16, 114, 3.0f * p, 8, 255, 220, 120, 255, 1);
    }
    if (msg[0]) {
        /* long messages over two lines */
        const size_t cut = 54;
        char a[64], b[110];
        SDL_strlcpy(a, msg, SDL_min(cut + 1, sizeof a));
        SDL_strlcpy(b, SDL_strlen(msg) > cut ? msg + cut : "", sizeof b);
        if (bad) { acr_hud_text(lx + 16, 98, a, WARN); if (b[0]) acr_hud_text(lx + 16, 112, b, WARN); }
        else acr_hud_text(lx + 16, 130, a, DIM);
    }

    const int installable = can_install(state);
    for (int r = 0; r < R_COUNT; ++r) {
        const float y = 158 + r * 22.0f;
        const int sel = r == ui->cur;
        if (sel) acr_hud_rect(lx + 8, y - 4, w - 16, 16, 60, 70, 40, 255, 1);
        const char *label = r == R_CHECK ? "CHECK FOR UPDATES" : r == R_BACK ? "BACK" : line;
        if (r == R_INSTALL) {
            if (state == UPDATE_CONFIRM && !ANDROID_UI) SDL_strlcpy(line, "RESTART AND UPDATE", sizeof line);
            else if (remote > 0) SDL_snprintf(line, sizeof line, "DOWNLOAD AND INSTALL BUILD %d", remote);
            else SDL_strlcpy(line, "DOWNLOAD AND INSTALL", sizeof line);
        }
        const int dim = r == R_INSTALL && !installable;
        if (dim) acr_hud_text(lx + 16, y, label, DIM);
        else acr_hud_text(lx + 16, y, label, sel ? 255 : 220, sel ? 220 : 230, sel ? 120 : 235);
    }
    acr_hud_text(lx + 16, 262, ANDROID_UI ? "A: CHOOSE   B: BACK   ANDROID ASKS BEFORE INSTALLING."
                                          : "A: CHOOSE   B: BACK   THE GAME RESTARTS TO UPDATE.", DIM);
    acr_hud_text(lx + 16, 278, "THE IMPORT AND YOUR SAVES ARE KEPT.", DIM);
}
