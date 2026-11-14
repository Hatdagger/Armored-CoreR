#ifndef _WIN32
#define _DEFAULT_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif
#include "acr_import.h"

#include <stdio.h>
#include <string.h>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <unistd.h>
#endif

/* Folder the program itself is in, ending in a separator ("" if unknown). */
static void program_dir(char *out, size_t size) {
    out[0] = 0;
#ifdef _WIN32
    DWORD n = GetModuleFileNameA(NULL, out, (DWORD)size);
    if (n == 0 || n >= size) { out[0] = 0; return; }
#else
    ssize_t n = readlink("/proc/self/exe", out, size - 1);
    if (n <= 0) { out[0] = 0; return; }
    out[n] = 0;
#endif
    char *slash = strrchr(out, '/');
#ifdef _WIN32
    char *back = strrchr(out, '\\');
    if (!slash || (back && back > slash)) slash = back;
#endif
    if (slash) slash[1] = 0; else out[0] = 0;
}

int main(int argc, char **argv) {
    /* acr-import <disc> <folder>, or just <disc> (a disc dropped on the
     * program): then it writes to "import" next to the program and waits for
     * Enter so the window stays readable. */
    if (argc != 2 && argc != 3) {
        fprintf(stderr, "usage: acr-import <disc.cue|disc.bin|disc.iso> [output folder]\n"
                        "Or drop your disc's .cue or .bin onto acr-import.\n");
        return 2;
    }
    char out_dir[4096];
    if (argc == 3) {
        snprintf(out_dir, sizeof out_dir, "%s", argv[2]);
    } else {
        char dir[4000];
        program_dir(dir, sizeof dir);
        snprintf(out_dir, sizeof out_dir, "%simport", dir);
        printf("Importing %s\ninto %s ...\n", argv[1], out_dir);
        fflush(stdout);
    }
    acr_import_stats stats;
    char err[512] = "";
    int rc = 0;
    if (!acr_import_disc(argv[1], out_dir, &stats, err, sizeof err)) {
        fprintf(stderr, "acr-import: %s\n", err);
        rc = 1;
    } else {
        printf("Copied %u files (%u kept as raw XA sectors).\n",
               (unsigned)(stats.files + stats.raw_files), (unsigned)stats.raw_files);
        printf("Unpacked %u archives into %u entries.\n",
               (unsigned)stats.archives, (unsigned)stats.archive_entries);
        if (stats.parts)
            printf("Wrote %u parts as readable files in %s/parts.\n", (unsigned)stats.parts, out_dir);
        if (stats.motion_sets)
            printf("Wrote %u animation sets' root motion in %s/motion.\n", (unsigned)stats.motion_sets, out_dir);
        if (stats.model_parts)
            printf("Wrote %u part models and %u skeletons in %s/models.\n",
                   (unsigned)stats.model_parts, (unsigned)stats.model_sets, out_dir);
        if (stats.archives_skipped)
            printf("Left %u .T files as they are: their tables did not check out.\n",
                   (unsigned)stats.archives_skipped);
        if (argc == 2) printf("\nDone. You can start armored-corer now.\n");
    }
    if (argc == 2) {
        printf("Press Enter to close.\n");
        fflush(stdout);
        (void)getchar();
    }
    return rc;
}
