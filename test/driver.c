/*
 * ff --- test driver.
 *
 * Runs a Forth source file through ff_eval() and compares captured
 * stdout against an expected-output file. Exit 0 on match, 1 on any
 * mismatch or I/O failure.
 *
 * Usage: ff_test_driver <input.ff> <expected.out>
 */

#include <ff.h>
#include <ff_platform.h>

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>


#define BUF_SIZE (64 * 1024)


typedef struct test_ctx
{
    char   *buf;
    size_t  size;
    size_t  capacity;
} test_ctx_t;


static int capture_vprintf(void *ctx, const char *fmt, va_list args)
{
    test_ctx_t *c = (test_ctx_t *)ctx;
    size_t room = c->capacity - c->size;
    if (!room)
        return 0;

    va_list args_copy;
    va_copy(args_copy, args);
    int n = vsnprintf(c->buf + c->size, room, fmt, args_copy);
    va_end(args_copy);

    if (n < 0)
        return n;
    if ((size_t)n >= room)
        n = (int)room - 1;
    c->size += (size_t)n;
    return n;
}

static char *read_file(const char *path, size_t *out_size)
{
    FILE *f = fopen(path, "rb");
    if (!f)
    {
        fprintf(stderr, "cannot open %s: ", path);
        perror(NULL);
        return NULL;
    }
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    rewind(f);
    if (size < 0)
    {
        fclose(f);
        return NULL;
    }
    char *buf = (char *)malloc((size_t)size + 1);
    if (!buf)
    {
        fclose(f);
        return NULL;
    }
    size_t got = fread(buf, 1, (size_t)size, f);
    fclose(f);
    buf[got] = '\0';
    if (out_size)
        *out_size = got;
    return buf;
}

int main(int argc, char **argv)
{
    if (argc != 3)
    {
        fprintf(stderr, "usage: %s <input.ff> <expected.out>\n", argv[0]);
        return 2;
    }

    const char *input_path = argv[1];
    const char *expect_path = argv[2];

    size_t src_size = 0;
    char *src = read_file(input_path, &src_size);
    if (!src)
        return 2;

    size_t expect_size = 0;
    char *expect = read_file(expect_path, &expect_size);
    if (!expect)
    {
        free(src);
        return 2;
    }

    test_ctx_t ctx =
    {
        .buf      = (char *)calloc(1, BUF_SIZE),
        .size     = 0,
        .capacity = BUF_SIZE
    };

    ff_platform_t p =
    {
        .context = &ctx,
        .vprintf = capture_vprintf,
        .vtracef = NULL
    };

    ff_t *ff = ff_new(&p);
    ff_error_t ec = ff_eval(ff, src);
    ff_free(ff);

    int rc = 0;
    if (ctx.size != expect_size || memcmp(ctx.buf, expect, expect_size) != 0)
    {
        fprintf(stderr, "=== %s ===\n", input_path);
        fprintf(stderr, "ff_eval returned %u\n", (unsigned)ec);
        fprintf(stderr, "EXPECTED (%zu bytes):\n%.*s\n",
                expect_size, (int)expect_size, expect);
        fprintf(stderr, "GOT (%zu bytes):\n%.*s\n",
                ctx.size, (int)ctx.size, ctx.buf);
        rc = 1;
    }

    free(ctx.buf);
    free(expect);
    free(src);
    return rc;
}
