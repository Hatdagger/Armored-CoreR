#include "acr_import.h"

#include <stdio.h>

int main(int argc, char **argv) {
    if (argc != 3) {
        fprintf(stderr, "usage: acr-import <disc.cue|disc.bin|disc.iso> <output folder>\n");
        return 2;
    }
    acr_import_stats stats;
    char err[512] = "";
    if (!acr_import_disc(argv[1], argv[2], &stats, err, sizeof err)) {
        fprintf(stderr, "acr-import: %s\n", err);
        return 1;
    }
    printf("Copied %u files (%u kept as raw XA sectors).\n",
           (unsigned)(stats.files + stats.raw_files), (unsigned)stats.raw_files);
    printf("Unpacked %u archives into %u entries.\n",
           (unsigned)stats.archives, (unsigned)stats.archive_entries);
    if (stats.archives_skipped)
        printf("Left %u .T files as they are: their tables did not check out.\n",
               (unsigned)stats.archives_skipped);
    return 0;
}
