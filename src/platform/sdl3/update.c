#include "update.h"

#include <SDL3/SDL.h>

#include <string.h>

#include "../../render/acr_render.h"

#define WHITE 220, 230, 235
#define DIM 120, 135, 145
#define HILITE 255, 220, 120
#define WARN 255, 120, 100

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

#else   /* the PC: no page (new builds come from GitHub's Actions tab) */

int update_supported(void) { return 0; }
static void do_check(void) {}
static void do_install(void) {}
static int local_build(void) { return 0; }
static int get_int(const char *name) { return SDL_strcmp(name, "state") == 0 ? UPDATE_FAILED : 0; }
static void get_string(const char *name, char *out, size_t size) {
    SDL_strlcpy(out, SDL_strcmp(name, "message") == 0 ? "UPDATES ARE FOR THE ANDROID APP." : "", size);
}
static void local_name(char *out, size_t size) { SDL_strlcpy(out, "PC BUILD", size); }

#endif

void update_open(update_ui *ui) {
    memset(ui, 0, sizeof *ui);
    do_check();
}

static int can_install(int state) {
    return state == UPDATE_AVAILABLE || state == UPDATE_NEEDS_PERMISSION ||
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
    case UPDATE_CONFIRM: status = "CONFIRM ON ANDROID'S INSTALL SCREEN."; break;
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
            if (remote > 0) SDL_snprintf(line, sizeof line, "DOWNLOAD AND INSTALL BUILD %d", remote);
            else SDL_strlcpy(line, "DOWNLOAD AND INSTALL", sizeof line);
        }
        const int dim = r == R_INSTALL && !installable;
        if (dim) acr_hud_text(lx + 16, y, label, DIM);
        else acr_hud_text(lx + 16, y, label, sel ? 255 : 220, sel ? 220 : 230, sel ? 120 : 235);
    }
    acr_hud_text(lx + 16, 262, "A: CHOOSE   B: BACK   ANDROID ASKS BEFORE INSTALLING.", DIM);
    acr_hud_text(lx + 16, 278, "THE IMPORT AND YOUR SAVES ARE KEPT.", DIM);
}
