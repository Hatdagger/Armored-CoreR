/* SYSTEM > SAVE / LOAD: the save list, as the original game's memory card
 * screen. Every save is one .acrsave file in the saves folder; the list
 * shows them all (pilot, AC, credits, when). A save file can be copied to
 * another PC: EXPORT puts a copy in the documents folder, and a save file
 * dropped on the window is imported into the list. */
#ifndef SAVE_LIST_H
#define SAVE_LIST_H

#include "garage.h"

#define SAVE_LIST_MAX 40

typedef enum save_list_mode { SL_MODE_SAVE, SL_MODE_LOAD, SL_MODE_P2 } save_list_mode;

typedef enum save_list_command {
    SL_NONE,
    SL_SAVE,      /* write the game into `path` (an existing save or a new file) */
    SL_LOAD,      /* load `path` (SL_MODE_P2: as player 2's save) */
    SL_BACK
} save_list_command;

typedef struct save_list_entry {
    char path[1200], file[96];
    char pilot[20], ac[20];
    long credits;
    long long mtime;     /* SDL_Time (ns) */
    int ok;              /* a readable save */
} save_list_entry;

typedef struct save_list {
    save_list_mode mode;
    char dir[1200];                      /* the saves folder (ends with a slash) */
    save_list_entry e[SAVE_LIST_MAX];
    int n;
    int row, top;
    int menu, menu_row;                  /* the A popup: SAVE / LOAD / EXPORT / CANCEL */
    char current[1200];                  /* the file the game is on (marked) */
    char path[1200];                     /* the command's file */
    char message[160];
    int message_ms;
} save_list;

/* The saves folder: ACR_SAVE_DIR, else beside ACR_SAVE, else the user data folder's saves/. */
void save_list_dir(char *out, size_t size);
void save_list_open(save_list *s, save_list_mode mode, const char *current);
void save_list_refresh(save_list *s);
/* A free name for a new save in the folder. */
void save_list_new_path(const save_list *s, char *out, size_t size);
save_list_command save_list_input(save_list *s, garage_action a);
/* A file dropped on the window: copied into the saves folder if it is a save. */
void save_list_import(save_list *s, const char *file);
void save_list_say(save_list *s, const char *text);
void save_list_draw(save_list *s, float hud_w, int elapsed_ms);

#endif
