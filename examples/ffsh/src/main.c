/*
 * ffsh --- fortissimo Forth shell.
 *
 * Uses a minimal fgets-based line reader rather than GNU readline so the
 * same source builds on Linux, macOS, Windows/MinGW, Windows/Clang, and
 * Windows/MSVC without external dependencies. There is no in-line
 * editing or tab completion; each line is simply read from stdin and
 * appended to a per-user history file as it is evaluated.
 *
 * Watchdog demo: Ctrl-C aborts a running evaluation via
 * ff_request_abort() (the async kill-flag path), and an optional
 * wall-clock budget set through the FFSH_TIMEOUT_MS env var aborts via
 * the ff_platform_t.watchdog polling callback. Both unwind cleanly to
 * the prompt with FF_ERR_ABORTED; the prompt's fgets is signal-safe
 * thanks to SA_RESTART on POSIX.
 */

#include "ffsh_version.h"

#include <ff.h>
#include <ff_platform.h>

#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#if defined(_WIN32)
#  include <direct.h>
#  include <windows.h>
#  define ffsh_mkdir(p) _mkdir(p)
#  define FFSH_PATH_SEP '\\'
#else
#  include <sys/stat.h>
#  include <sys/types.h>
#  define ffsh_mkdir(p) mkdir((p), 0700)
#  define FFSH_PATH_SEP '/'
#endif


#define FFSH_LINE_SIZE  4096
#define FFSH_PATH_SIZE  1024
/* Final path = dir + path-separator + "history.ff" + NUL. Reserve 32
   bytes of slack so the directory buffer is strictly smaller than the
   final-path buffer; that's what GCC's -Wformat-truncation verifies. */
#define FFSH_DIR_SIZE   (FFSH_PATH_SIZE - 32)


/* Resolve the per-user history file path:
 *
 *   - $FFSH_HISTORY (override) — used verbatim if set.
 *   - On Linux/macOS/BSD: $XDG_DATA_HOME/ff/history.ff, falling back to
 *     $HOME/.local/share/ff/history.ff.
 *   - On Windows: %APPDATA%\ff\history.ff.
 *   - Otherwise: ./history.ff (legacy behaviour).
 *
 * The directory is created on demand. Returns a pointer to a static
 * buffer; the caller does not own it.
 */
static const char *ffsh_history_path(void)
{
    static char path[FFSH_PATH_SIZE];

    const char *over = getenv("FFSH_HISTORY");
    if (over && *over)
    {
        snprintf(path, sizeof(path), "%s", over);
        return path;
    }

    char dir[FFSH_DIR_SIZE];

#if defined(_WIN32)
    const char *base = getenv("APPDATA");
    if (!base || !*base)
        return "history.ff";
    snprintf(dir, sizeof(dir), "%s%cff", base, FFSH_PATH_SEP);
#else
    const char *xdg = getenv("XDG_DATA_HOME");
    if (xdg && *xdg)
    {
        snprintf(dir, sizeof(dir), "%s/ff", xdg);
    }
    else
    {
        const char *home = getenv("HOME");
        if (!home || !*home)
            return "history.ff";
        snprintf(dir, sizeof(dir), "%s/.local/share/ff", home);
    }
#endif

    /* Best-effort directory creation. If it already exists or the
       parent isn't writable we fall through to the open call, which
       will fail and the history append silently skips that line. */
    ffsh_mkdir(dir);

    snprintf(path, sizeof(path), "%s%chistory.ff", dir, FFSH_PATH_SEP);
    return path;
}


static char *ffsh_readline(const char *prompt)
{
    static char buffer[FFSH_LINE_SIZE];

    fputs(prompt, stdout);
    fflush(stdout);

    if (!fgets(buffer, sizeof(buffer), stdin))
        return NULL;

    /* Strip trailing \n and (on Windows) \r. */
    size_t len = strlen(buffer);
    while (len > 0 && (buffer[len - 1] == '\n' || buffer[len - 1] == '\r'))
        buffer[--len] = '\0';

    return buffer;
}

static void ffsh_history_append(const char *line)
{
    FILE *f = fopen(ffsh_history_path(), "a");
    if (!f)
        return;
    fputs(line, f);
    fputc('\n', f);
    fclose(f);
}

static int ffsh_vprintf(void *ctx, const char *fmt, va_list args)
{
    (void) ctx;

    va_list args_copy;
    va_copy(args_copy, args);
    const int n = vprintf(fmt, args_copy);
    va_end(args_copy);

    return n;
}


/* --- Watchdog demo -------------------------------------------------
 *
 * Two mechanisms cooperate, both unwinding through FF_ERR_ABORTED:
 *
 *  - Async kill-flag: SIGINT (Ctrl-C) handler calls ff_request_abort
 *    on the engine. Picked up at the next dispatch boundary.
 *  - Polling watchdog: a wall-clock deadline checked by the engine
 *    every watchdog_interval opcodes. Enabled by FFSH_TIMEOUT_MS.
 *
 * The watchdog context is a pointer to g_wd. The SIGINT handler reads
 * g_ff (set just before ff_eval, cleared just after) — sig_atomic_t so
 * the read is safe from a signal handler.
 */

typedef struct ffsh_watchdog_state
{
    uint64_t deadline_ms;   /* Monotonic-ms deadline; 0 = no time budget. */
} ffsh_wd_t;

static ffsh_wd_t g_wd;
static volatile sig_atomic_t g_in_eval = 0;
static ff_t * volatile g_ff = NULL;

static uint64_t ffsh_now_ms(void)
{
#if defined(_WIN32)
    return (uint64_t)GetTickCount64();
#else
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000ULL + (uint64_t)ts.tv_nsec / 1000000ULL;
#endif
}

static ff_watchdog_action_t ffsh_watchdog(void *ctx, uint64_t opcodes_run)
{
    (void) opcodes_run;
    ffsh_wd_t *wd = (ffsh_wd_t *)ctx;
    if (wd->deadline_ms && ffsh_now_ms() >= wd->deadline_ms)
        return FF_WD_ABORT;
    return FF_WD_CONTINUE;
}

static void ffsh_sigint(int sig)
{
    (void) sig;
    if (g_in_eval && g_ff)
        ff_request_abort(g_ff);
}

static void ffsh_install_sigint(void)
{
#if defined(_WIN32)
    signal(SIGINT, ffsh_sigint);
#else
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = ffsh_sigint;
    sa.sa_flags   = SA_RESTART;   /* don't kill fgets at the prompt */
    sigemptyset(&sa.sa_mask);
    sigaction(SIGINT, &sa, NULL);
#endif
}

int main(int argc, char **argv)
{
    (void) argc;
    (void) argv;

    setbuf(stdout, NULL);
    setbuf(stderr, NULL);

    /* Parse FFSH_TIMEOUT_MS. 0 / unset / unparseable disables the
       polling time-budget. The async Ctrl-C path stays on regardless. */
    uint32_t timeout_ms = 0;
    const char *timeout_env = getenv("FFSH_TIMEOUT_MS");
    if (timeout_env && *timeout_env)
    {
        char *end = NULL;
        unsigned long v = strtoul(timeout_env, &end, 10);
        if (end != timeout_env && v > 0 && v < (1UL << 31))
            timeout_ms = (uint32_t)v;
    }

    /* Pick a watchdog interval that's responsive without bloating
       per-opcode overhead. ~4 K opcodes ≈ 30 µs on this hardware, fine
       for sub-second time-budget enforcement. The engine's default
       (65 K) only matters if no callback is registered, but we set it
       explicitly anyway. */
    ff_platform_t p =
    {
        .context           = &g_wd,
        .vprintf           = ffsh_vprintf,
        .vtracef           = NULL,
        .watchdog          = timeout_ms ? ffsh_watchdog : NULL,
        .watchdog_interval = 4096
    };

    ff_t *ff = ff_new(&p);
    g_ff = ff;

    ffsh_install_sigint();

    ff_printf(ff, "%s\n", ff_banner(ff));
    if (timeout_ms)
        ff_printf(ff, "Watchdog: aborting after %u ms per line.\n", timeout_ms);
    ff_printf(ff, "Press Ctrl-C to abort a running evaluation.\n");

    for (;;)
    {
        char prompt[32];
        snprintf(prompt, sizeof(prompt), "%s ", ff_prompt(ff));

        char *line = ffsh_readline(prompt);
        if (!line)
            break;

        if (*line)
        {
            g_wd.deadline_ms = timeout_ms
                ? ffsh_now_ms() + timeout_ms
                : 0;
            g_in_eval = 1;
            ff_error_t ec = ff_eval(ff, line);
            g_in_eval = 0;

            ffsh_history_append(line);

            if (ec != 0)
                fprintf(stderr, "Error: %s\n", ff_strerror(ff));
        }
    }

    g_ff = NULL;
    ff_free(ff);

    return EXIT_SUCCESS;
}
