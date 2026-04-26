/*
 * ffsh --- fortissimo Forth shell.
 *
 * Uses a minimal fgets-based line reader rather than GNU readline so the
 * same source builds on Linux, macOS, Windows/MinGW, Windows/Clang, and
 * Windows/MSVC without external dependencies. There is no in-line
 * editing or tab completion; each line is simply read from stdin and
 * appended to history.ff as it is evaluated.
 */

#include "ffsh_version.h"

#include <ff.h>
#include <ff_platform.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>


#define FFSH_LINE_SIZE  4096

static const char FFSH_HISTORY_FILE[] = "history.ff";


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
    FILE *f = fopen(FFSH_HISTORY_FILE, "a");
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
