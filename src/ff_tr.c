/**
 * @file ff_tr.c
 * @brief Translations: picking a text by language, reading texts kept
 *        for later, and the language each engine shows its texts in.
 */

#include "ff_tr.h"

#include <ff_p.h>

#include <stdbool.h>
#include <string.h>


/* The languages ff's own texts are written in, English first, each with
   its name in that language. ff_tr_langs() hands out the codes. */
static const char *const g_langs[] =
{
    "en",
#if FF_WITH_TR
    "ru",
#endif
    NULL
};

static const char *const g_lang_names[] =
{
    "English",
#if FF_WITH_TR
    "Русский",
#endif
};


/**
 * Read the two-letter code a language code or locale name starts with:
 * two ASCII letters, then the end or a separator ("ru", "ru_RU.UTF-8",
 * "ru-RU", "en@quot"), compared without regard to case.
 *
 * @param name Code or locale name; may be NULL.
 * @param code Out: the code in lower case, NUL-terminated.
 * @return false for NULL, "C", "POSIX", a three-letter code and the like.
 */
static bool ff_tr_code(const char *name, char code[3])
{
    if (!name)
        return false;

    char c[2];
    for (int i = 0; i < 2; ++i)
    {
        char ch = name[i];
        if (ch >= 'A' && ch <= 'Z')
            ch = (char)(ch - 'A' + 'a');
        if (ch < 'a' || ch > 'z')
            return false;
        c[i] = ch;
    }

    const char sep = name[2];
    if (sep != '\0' && sep != '_' && sep != '-' && sep != '.' && sep != '@')
        return false;

    code[0] = c[0];
    code[1] = c[1];
    code[2] = '\0';
    return true;
}

/**
 * @return true if @p t starts with a whole tag: FF_TR_SEP and two more
 *         bytes, neither of them a NUL.
 */
static bool ff_tr_tagged(const char *t)
{
    return t[0] == FF_TR_SEP && t[1] && t[2];
}


/** @copydoc ff_tr */
const char *ff_tr(const char *lang, const char *tr1, const char *tr2)
{
    char code[3];
    const bool have = ff_tr_code(lang, code);

    const char *const trs[FF_TR_MAX] = { tr1, tr2 };
    const char *en = NULL;
    const char *first = NULL;
    for (size_t i = 0; i < FF_TR_MAX; ++i)
    {
        const char *t = trs[i];
        if (!t)
            continue;
        if (!ff_tr_tagged(t))
        {
            /* Untagged: the same in every language. */
            if (*t && !first)
                first = t;
            continue;
        }

        const char *text = t + 3;
        if (!*text)
            continue;   /* not translated yet */
        if (have && t[1] == code[0] && t[2] == code[1])
            return text;
        if (t[1] == 'e' && t[2] == 'n')
            en = text;
        if (!first)
            first = text;
    }

    return en ? en : first ? first : "";
}

/* Append @p n bytes of @p s to @p buf, snprintf-fashion: @p pos counts
   what would have been written. */
static void ff_tr_put(char *buf, size_t size, size_t *pos,
                      const char *s, size_t n)
{
    if (*pos + 1 < size)
    {
        size_t room = size - 1 - *pos;
        memcpy(buf + *pos, s, n < room ? n : room);
    }
    *pos += n;
}

/** @copydoc ff_tr_copy */
size_t ff_tr_copy(const char *lang, const char *text, char *buf, size_t size)
{
    if (!text)
        text = "";

    char code[3];
    const bool have = ff_tr_code(lang, code);

    /* Before the first tag: the same in every language. */
    const char *tag = strchr(text, FF_TR_SEP);
    const char *shared_end = tag ? tag : text + strlen(text);

    /* The translations: each runs from its tag to the next tag or the
       end. Pick the one in lang, else the English one, else the first
       that has a text. */
    const char *pick = NULL, *pick_end = NULL;
    const char *en = NULL, *en_end = NULL;
    const char *first = NULL, *first_end = NULL;
    for (const char *p = tag; p && ff_tr_tagged(p); )
    {
        const char *seg = p + 3;
        const char *next = strchr(seg, FF_TR_SEP);
        const char *end = next ? next : seg + strlen(seg);
        if (end > seg)
        {
            if (have && !pick && p[1] == code[0] && p[2] == code[1])
            {
                pick = seg;
                pick_end = end;
            }
            if (!en && p[1] == 'e' && p[2] == 'n')
            {
                en = seg;
                en_end = end;
            }
            if (!first)
            {
                first = seg;
                first_end = end;
            }
        }
        p = next;
    }
    if (!pick)
    {
        pick = en ? en : first;
        pick_end = en ? en_end : first_end;
    }

    size_t pos = 0;
    ff_tr_put(buf, size, &pos, text, (size_t)(shared_end - text));
    if (pick)
        ff_tr_put(buf, size, &pos, pick, (size_t)(pick_end - pick));
    if (size > 0)
    {
        buf[pos < size ? pos : size - 1] = '\0';
        if (pos >= size)
            ff_utf8_cut(buf);   /* cut short: not inside a character */
    }
    return pos;
}

/** @copydoc ff_lang */
const char *ff_lang(const ff_t *ff)
{
    return ff ? ff->lang : "en";
}

/**
 * @return The index in g_langs of the language @p locale names, if ff has
 *         texts in it; -1 otherwise.
 */
static int ff_tr_lang_index(const char *locale)
{
    char code[3];
    if (!ff_tr_code(locale, code))
        return -1;
    for (int i = 0; g_langs[i]; ++i)
        if (memcmp(g_langs[i], code, sizeof(code)) == 0)
            return i;
    return -1;
}

/** @copydoc ff_set_lang */
const char *ff_set_lang(ff_t *ff, const char *lang)
{
    /* The C locale, under any of its names, is English. */
    const bool c_locale = !lang
                          || strcmp(lang, "C") == 0
                          || strncmp(lang, "C.", 2) == 0
                          || strcmp(lang, "POSIX") == 0;
    char code[3] = "en";
    if (!c_locale && !ff_tr_code(lang, code))
        return NULL;

    /* What is in effect: the language named if ff has texts in it, else
       English. */
    const int i = c_locale ? -1 : ff_tr_lang_index(code);
    memcpy(ff->lang, i < 0 ? "en" : g_langs[i], sizeof(ff->lang));
    return ff->lang;
}

/** @copydoc ff_tr_lang_of */
const char *ff_tr_lang_of(const char *locale)
{
    const int i = ff_tr_lang_index(locale);
    return i < 0 ? NULL : g_langs[i];
}

/** @copydoc ff_tr_langs */
const char *const *ff_tr_langs(void)
{
    return g_langs;
}

/** @copydoc ff_tr_lang_name */
const char *ff_tr_lang_name(const char *lang)
{
    const int i = ff_tr_lang_index(lang);
    return i < 0 ? NULL : g_lang_names[i];
}
