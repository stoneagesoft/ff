/*
 * ffsh --- fortissimo Forth shell.
 *
 * Uses a minimal fgets-based line reader rather than GNU readline so the
 * same source builds on Linux, macOS, Windows/MinGW, Windows/Clang, and
 * Windows/MSVC without external dependencies. There is no in-line
 * editing or tab completion; each line is simply read from stdin and
 * appended to a per-user history file as it is evaluated.
 */

#include "ffsh_version.h"

#include <ff.h>
#include <ff_platform.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(_WIN32)
#  include <direct.h>
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

int main(int argc, char **argv)
{
    (void) argc;
    (void) argv;

    setbuf(stdout, NULL);
    setbuf(stderr, NULL);

    ff_platform_t p =
    {
        .context = NULL,
        .vprintf = ffsh_vprintf,
        .vtracef = NULL
    };

    ff_t *ff = ff_new(&p);

    ff_printf(ff, "%s\n", ff_banner(ff));

    for (;;)
    {
        char prompt[32];
        snprintf(prompt, sizeof(prompt), "%s ", ff_prompt(ff));

        char *line = ffsh_readline(prompt);
        if (!line)
            break;

        if (*line)
        {
            ff_error_t ec = ff_eval(ff, line);

            ffsh_history_append(line);

            if (ec != 0)
                fprintf(stderr, "Error: %s\n", ff_strerror(ff));
        }
    }

    ff_free(ff);

    return EXIT_SUCCESS;
}
