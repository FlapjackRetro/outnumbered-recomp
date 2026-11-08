/* OutNumbered! recomp: host entry point.
 *
 *   sso [game-dir]             game-dir holds SSO.EXE and the .GRA/.DAT files
 *   sso SSO.EXE [game-dir]     explicit executable (packed or unpacked)
 *
 * With no arguments the game folder is the current directory, else its parent
 * (the recomp folder normally sits inside the game folder).
 */
#include "recomp/runtime16.h"
#include "recomp/frontend.h"
#include <dirent.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

static char g_exe[1024], g_dir[1024];
static int g_rc;
static volatile int g_done;
static pthread_t g_thread;

static void *run(void *arg)
{
    (void)arg;
    CPU cpu; cpu_init(&cpu);
    if (cpu_alloc_mem(&cpu) || sso_load_image(&cpu, g_exe)) { g_rc = 1; g_done = 1; return NULL; }
    sso_run(&cpu);
    g_rc = sso_exit_code;
    g_done = 1;
    return NULL;
}

static void start_guest(void)
{
    /* lifted code nests C calls as deeply as the guest nests calls: big stack */
    pthread_attr_t at; pthread_attr_init(&at); pthread_attr_setstacksize(&at, (size_t)512 << 20);
    if (pthread_create(&g_thread, &at, run, NULL)) { perror("pthread_create"); exit(1); }
}

/* <dir>/SSO.EXE, matched case-insensitively; 0 if found */
static int find_exe(const char *dir, char *out, size_t n)
{
    DIR *d = opendir(dir);
    if (!d) return -1;
    struct dirent *e; int ok = -1;
    while ((e = readdir(d))) if (!strcasecmp(e->d_name, "SSO.EXE")) { snprintf(out, n, "%s/%s", dir, e->d_name); ok = 0; break; }
    closedir(d);
    return ok;
}

static int ends_with_exe(const char *s)
{
    size_t n = strlen(s);
    return n > 4 && !strcasecmp(s + n - 4, ".exe");
}

int main(int argc, char **argv)
{
    if (argc > 1 && (!strcmp(argv[1], "-h") || !strcmp(argv[1], "--help"))) {
        fprintf(stderr, "usage: %s [game-dir]\n       %s SSO.EXE [game-dir]\n", argv[0], argv[0]);
        return 2;
    }
    if (argc > 1 && ends_with_exe(argv[1])) {
        snprintf(g_exe, sizeof g_exe, "%s", argv[1]);
        if (argc > 2) snprintf(g_dir, sizeof g_dir, "%s", argv[2]);
        else {                                       /* data beside the exe */
            snprintf(g_dir, sizeof g_dir, "%s", argv[1]);
            char *s = strrchr(g_dir, '/'), *b = strrchr(g_dir, '\\');
            if (b > s) s = b;
            if (s) *s = 0; else strcpy(g_dir, ".");
        }
    } else {
        const char *cands[2] = { argc > 1 ? argv[1] : ".", argc > 1 ? NULL : ".." };
        for (int i = 0; i < 2 && cands[i]; i++)
            if (!find_exe(cands[i], g_exe, sizeof g_exe)) { snprintf(g_dir, sizeof g_dir, "%s", cands[i]); break; }
        if (!g_exe[0]) {
            fprintf(stderr, "SSO.EXE not found in %s. Pass the game folder: %s <game-dir>\n",
                    argc > 1 ? argv[1] : ". or ..", argv[0]);
            return 2;
        }
    }
    sso_set_game_dir(g_dir);
    fe_run(start_guest, &g_done);
    pthread_join(g_thread, NULL);
    return g_rc;
}
