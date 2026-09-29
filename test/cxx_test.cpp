/*
 * A C++ host using nothing but the public headers. Without their
 * extern "C" blocks it didn't link: C++ looked for mangled names.
 */

#include <ff.h>
#include <ff_platform.h>

#include <cstdio>

static int out(void *ctx, const char *fmt, va_list args)
{
    (void)ctx;
    return std::vprintf(fmt, args);
}

static void twice(ff_t *ff)
{
    int64_t n;
    if (ff_pop_int(ff, &n))
        ff_push_int(ff, 2 * n);
}

int main()
{
    ff_platform_t p = {};
    p.vprintf = out;

    ff_t *ff = ff_new(&p);
    if (!ff)
        return 1;

    static const ff_native_word_t words[] =
    {
        FF_NATIVE("twice", twice, "( n -- 2n )  Doubles n."),
        FF_NATIVE_END
    };
    ff_register(ff, words);

    int64_t v = 0;
    bool ok = ff_eval(ff, "21 twice") == FF_OK
              && ff_pop_int(ff, &v) && v == 42
              && ff_exec(ff, ff_find(ff, "twice")) == false
              && ff_errno(ff) == FF_ERR_STACK_UNDER;

    ff_free(ff);
    std::printf("%s\n", ok ? "ok" : "FAILED");
    return ok ? 0 : 1;
}
