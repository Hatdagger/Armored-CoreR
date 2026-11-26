#include "disc_pick.h"

#include <stdio.h>
#include <string.h>

#ifdef SDL_PLATFORM_ANDROID
#include <unistd.h>
#endif

/* The picker answers on its own thread (Android: the UI thread), so the
 * answer is handed over through an atomic flag. */
static SDL_AtomicInt g_state;        /* 0 waiting / nothing, 1 picked, -1 closed */
static char g_picked[2048];

static void SDLCALL picked(void *userdata, const char *const *files, int filter) {
    (void)userdata;
    (void)filter;
    if (files && files[0]) {
        SDL_strlcpy(g_picked, files[0], sizeof g_picked);
        SDL_SetAtomicInt(&g_state, 1);
    } else {
        SDL_SetAtomicInt(&g_state, -1);
    }
    /* wake the import screen's SDL_WaitEvent */
    SDL_Event e;
    SDL_zero(e);
    e.type = SDL_EVENT_USER;
    SDL_PushEvent(&e);
}

void disc_pick_open(SDL_Window *window) {
    SDL_SetAtomicInt(&g_state, 0);
    /* No filters: Android filters by file type, and a .bin or .cue has none
     * it knows, so a filter would hide the disc. */
    SDL_ShowOpenFileDialog(picked, NULL, window, NULL, 0, NULL, false);
}

int disc_pick_poll(char *out, size_t out_size) {
    const int s = SDL_GetAtomicInt(&g_state);
    if (s == 1) SDL_strlcpy(out, g_picked, out_size);
    if (s) SDL_SetAtomicInt(&g_state, 0);
    return s;
}

int disc_pick_local(const char *picked_path, disc_pick_file *keep, char *path, size_t path_size,
                    char *err, size_t err_size) {
    SDL_zerop(keep);
    if (SDL_strncmp(picked_path, "content://", 10) != 0) {
        SDL_strlcpy(path, picked_path, path_size);
        return 1;
    }
#ifdef SDL_PLATFORM_ANDROID
    keep->io = SDL_IOFromFile(picked_path, "rb");
    if (!keep->io) { SDL_snprintf(err, err_size, "Could not open the picked file:\n%s", SDL_GetError()); return 0; }
    const Sint64 size = SDL_GetIOSize(keep->io);
    if (size < 1024 * 1024) {
        /* a .cue only names its .bin, which Android doesn't let us reach */
        char head[8] = "";
        SDL_ReadIO(keep->io, head, 4);
        SDL_snprintf(err, err_size, SDL_strncmp(head, "FILE", 4) == 0 || SDL_strncmp(head, "REM", 3) == 0
                     ? "That is the .cue file. Pick the .bin file\nthat goes with it."
                     : "That file is too small to be the disc image.");
        disc_pick_close(keep);
        return 0;
    }
    FILE *fp = (FILE *)SDL_GetPointerProperty(SDL_GetIOProperties(keep->io), SDL_PROP_IOSTREAM_STDIO_FILE_POINTER, NULL);
    if (!fp) { SDL_snprintf(err, err_size, "Could not read the picked file."); disc_pick_close(keep); return 0; }
    SDL_snprintf(path, path_size, "/proc/self/fd/%d", fileno(fp));
    /* The importer tells a .bin (2352-byte sectors) from an .iso (2048) by
     * the name; a picked file has none, so an .iso gets a link named .iso. */
    if (size % 2352 != 0 && size % 2048 == 0) {
        char *pref = SDL_GetPrefPath("Hatdagger", "Armored-CoreR");
        SDL_snprintf(keep->link, sizeof keep->link, "%spicked-disc.iso", pref ? pref : "");
        SDL_free(pref);
        unlink(keep->link);
        if (symlink(path, keep->link) == 0) SDL_strlcpy(path, keep->link, path_size);
        else keep->link[0] = 0;
    }
    FILE *probe = fopen(path, "rb");
    if (!probe) {
        SDL_snprintf(err, err_size, "Could not read the picked file from here.\n"
                     "Copy the .bin into Downloads and pick it there.");
        disc_pick_close(keep);
        return 0;
    }
    fclose(probe);
    return 1;
#else
    (void)path; (void)path_size;
    SDL_snprintf(err, err_size, "That file can't be opened here.");
    return 0;
#endif
}

void disc_pick_close(disc_pick_file *keep) {
#ifdef SDL_PLATFORM_ANDROID
    if (keep->link[0]) unlink(keep->link);
#endif
    if (keep->io) SDL_CloseIO(keep->io);
    SDL_zerop(keep);
}
