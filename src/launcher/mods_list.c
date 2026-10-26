/* The launcher's asset mod list (mods_list.h). */
#include "mods_list.h"

#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>

static int is_dir(const char *p)
{
    struct stat st;
    return stat(p, &st) == 0 && S_ISDIR(st.st_mode);
}

/* The game's rule (kernel_mods.c): one folder, no separators, no "..". A
 * stricter rule here would drop a mod the game applies when Play saves. */
static int name_ok(const char *s)
{
    return *s && !strchr(s, '/') && !strchr(s, '\\') && !strstr(s, "..");
}

static int add(const char *dir, const char *name, int on, ModEntry *mods, int n, int max)
{
    char path[1200], l1[128] = "", l2[160] = "";
    FILE *f;
    ModEntry *m;
    int i;
    for (i = 0; i < n; i++)
        if (!strcmp(mods[i].name, name))
            return n;
    snprintf(path, sizeof path, "%s/%s", dir, name);
    if (n == max || !name_ok(name) || strlen(name) >= sizeof mods[0].name || !is_dir(path))
        return n;
    m = &mods[n];
    snprintf(m->name, sizeof m->name, "%s", name);
    m->on = on;
    snprintf(path, sizeof path, "%s/%s/mod.txt", dir, name);
    if ((f = fopen(path, "r"))) {
        if (fgets(l1, sizeof l1, f))
            l1[strcspn(l1, "\r\n")] = 0;
        if (fgets(l2, sizeof l2, f))
            l2[strcspn(l2, "\r\n")] = 0;
        fclose(f);
    }
    snprintf(m->label, sizeof m->label, "%s%s%s", *l1 ? l1 : name, *l2 ? ": " : "", l2);
    return n + 1;
}

static int by_name(const void *a, const void *b)
{
    return strcasecmp(((const ModEntry *)a)->name, ((const ModEntry *)b)->name);
}

int mods_list_scan(const char *dir, ModEntry *mods, int max)
{
    char path[1200], line[256];
    FILE *f;
    DIR *d;
    struct dirent *de;
    int n = 0, first_off;
    snprintf(path, sizeof path, "%s/load-order.txt", dir);
    if ((f = fopen(path, "r"))) {
        while (fgets(line, sizeof line, f)) {
            char *s = line, *e;
            while (*s == ' ' || *s == '\t')
                s++;
            e = s + strlen(s);
            while (e > s && (e[-1] == '\n' || e[-1] == '\r' || e[-1] == ' ' || e[-1] == '\t'))
                *--e = 0;
            if (*s != '#')
                n = add(dir, s, 1, mods, n, max);
        }
        fclose(f);
    }
    first_off = n;
    if ((d = opendir(dir))) {
        while ((de = readdir(d)))
            if (strcmp(de->d_name, ".") && strcmp(de->d_name, ".."))
                n = add(dir, de->d_name, 0, mods, n, max);
        closedir(d);
    }
    qsort(mods + first_off, (size_t)(n - first_off), sizeof *mods, by_name);
    return n;
}

int mods_list_save(const char *dir, const ModEntry *mods, int n)
{
    char path[1200];
    FILE *f;
    int i;
    if (!is_dir(dir))
        return 0;
    snprintf(path, sizeof path, "%s/load-order.txt", dir);
    if (!(f = fopen(path, "w")))
        return 0;
    fprintf(f, "# Enabled asset mods, one folder name per line; a later line wins (written by the launcher)\n");
    for (i = 0; i < n; i++)
        if (mods[i].on)
            fprintf(f, "%s\n", mods[i].name);
    return fclose(f) == 0;
}
