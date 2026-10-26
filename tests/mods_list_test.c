/* src/launcher/mods_list.c: scan order, labels, skipped entries, save round trip. */
#include "../src/launcher/mods_list.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static int fails;
#define CHECK(c) do { if (!(c)) { fprintf(stderr, "FAIL %s:%d %s\n", __FILE__, __LINE__, #c); fails++; } } while (0)

static char root[256];

static void put(const char *rel, const char *text)
{
    char p[512];
    FILE *f;
    snprintf(p, sizeof p, "%s/%s", root, rel);
    if (!(f = fopen(p, "w"))) { perror(p); exit(2); }
    fputs(text, f);
    fclose(f);
}

static void dir(const char *rel)
{
    char p[512];
    snprintf(p, sizeof p, "%s/%s", root, rel);
    mkdir(p, 0755);
}

static void slurp(const char *rel, char *out, size_t n)
{
    char p[512];
    FILE *f;
    size_t k = 0;
    snprintf(p, sizeof p, "%s/%s", root, rel);
    if ((f = fopen(p, "r"))) {
        k = fread(out, 1, n - 1, f);
        fclose(f);
    }
    out[k] = 0;
}

int main(void)
{
    ModEntry m[MODS_LIST_MAX];
    char text[512], missing[300];
    int n;
    snprintf(root, sizeof root, "%s/mods_list_test_%d", getenv("TMPDIR") ? getenv("TMPDIR") : "/tmp", (int)getpid());
    mkdir(root, 0755);
    dir("a"); dir("b"); dir("c"); dir(".beta");
    put("notdir", "x");
    put("a/mod.txt", "Name A\r\ndoes A\r\n");
    put("load-order.txt", "# comment\n  c  \nmissing\n../x\na\n.beta\n");

    n = mods_list_scan(root, m, MODS_LIST_MAX);
    CHECK(n == 4);
    CHECK(!strcmp(m[0].name, "c") && m[0].on);         /* load order first */
    CHECK(!strcmp(m[1].name, "a") && m[1].on);
    CHECK(!strcmp(m[2].name, ".beta") && m[2].on);     /* a dot name, as the game takes it */
    CHECK(!strcmp(m[3].name, "b") && !m[3].on);        /* then the rest, off */
    CHECK(!strcmp(m[1].label, "Name A: does A"));
    CHECK(!strcmp(m[3].label, "b"));

    m[0].on = 0;                                       /* c off */
    CHECK(mods_list_save(root, m, n) == 1);
    slurp("load-order.txt", text, sizeof text);
    CHECK(strstr(text, "\na\n") != NULL && strstr(text, "\n.beta\n") != NULL);
    CHECK(strstr(text, "\nc\n") == NULL && strstr(text, "\nb\n") == NULL);

    n = mods_list_scan(root, m, MODS_LIST_MAX);
    CHECK(n == 4);
    CHECK(!strcmp(m[0].name, "a") && m[0].on);
    CHECK(!strcmp(m[1].name, ".beta") && m[1].on);
    CHECK(!strcmp(m[2].name, "b") && !m[2].on);        /* off ones by name */
    CHECK(!strcmp(m[3].name, "c") && !m[3].on);

    snprintf(missing, sizeof missing, "%s/nope", root);
    CHECK(mods_list_scan(missing, m, MODS_LIST_MAX) == 0);
    CHECK(mods_list_save(missing, m, n) == 0);
    if (!fails)
        printf("mods_list: all checks passed\n");
    return fails != 0;
}
