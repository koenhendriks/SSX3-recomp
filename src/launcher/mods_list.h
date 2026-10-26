/* The launcher's asset mod list: the folders in mods/ and mods/load-order.txt,
 * which the game reads (docs/MODS.md). POSIX; the Windows launcher can take it
 * once it gets a FindFirstFile scan. */
#ifndef SSX3_MODS_LIST_H
#define SSX3_MODS_LIST_H

#define MODS_LIST_MAX 64

typedef struct {
    char name[128];             /* the folder */
    char label[300];            /* mod.txt line 1 (the name) ": " line 2, or the folder */
    int on;
} ModEntry;

/* Enabled mods in load order, then the other folders by name (off). */
int mods_list_scan(const char *dir, ModEntry *mods, int max);

/* Writes dir/load-order.txt with the enabled mods in list order. 0 if dir is missing. */
int mods_list_save(const char *dir, const ModEntry *mods, int n);

#endif
