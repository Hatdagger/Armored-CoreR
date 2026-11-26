/* Choosing the disc image with the system's own file picker (Android's
 * "open a file" screen, the Windows / Linux file dialog), for the import
 * screen. */
#ifndef DISC_PICK_H
#define DISC_PICK_H

#include <SDL3/SDL.h>

/* Shows the picker; the answer comes later through disc_pick_poll. */
void disc_pick_open(SDL_Window *window);

/* 1: a file was picked (its path or Android content:// URI in `out`),
 * -1: the picker was closed without one, 0: no answer yet. */
int disc_pick_poll(char *out, size_t out_size);

/* The picked file as a path the importer can open (fopen). A plain path
 * is returned as it is. An Android content:// URI is opened and handed
 * over as /proc/self/fd/N, so the 700 MB image is read where it is, not
 * copied; `keep` holds it open until disc_pick_close. 0 with the reason
 * in err. */
typedef struct disc_pick_file {
    SDL_IOStream *io;
    char link[1100];     /* a .iso link made for a 2048-byte-sector image */
} disc_pick_file;
int  disc_pick_local(const char *picked, disc_pick_file *keep, char *path, size_t path_size,
                     char *err, size_t err_size);
void disc_pick_close(disc_pick_file *keep);

#endif
