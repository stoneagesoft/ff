/**
 * @file ff_tr.h
 * @brief Translations: the texts ff shows people, in the language each
 *        engine is set to.
 *
 * Every text ff shows people — messages, `man` entries, table labels —
 * is written where it is used, in every language it has, each literal
 * marked with its language:
 *
 * ~~~{.c}
 * ff_tracef(ff, FF_SEV_ERROR | FF_ERR_UNDEFINED,
 *           FF_TR(ff, FF_TR_EN("'%s' undefined."),
 *                     FF_TR_RU("Слово «%s» не определено.")), name);
 * ~~~
 *
 * A marker puts a tag in front of its literal: the byte @ref FF_TR_SEP
 * and the language's two-letter ISO 639-1 code. @ref FF_TR picks, when
 * it runs, the text in the engine's language (ff_set_lang()), else the
 * English one; the order of the translations doesn't matter. A text not
 * translated yet is left out, or written as `FF_TR_RU("")`, and shows in
 * English. GCC and Clang check every translation of a format against the
 * call's arguments, through FF_FORMAT_ARG on ff_tr().
 *
 * A text kept for later — a word table's `man` entry — is the markers
 * written next to each other, with no comma between them: one string
 * holding every language, which ff_tr_copy() reads when the text is
 * wanted. Text before the first tag is the same in every language.
 *
 * A build with FF_WITH_TR off (see ff_version.h) keeps English only:
 * every other marker expands to nothing, FF_TR is the English literal,
 * and the texts kept for later are English too. A check build (CMake
 * `FF_TR_ONLY=ru`) keeps that one language only, so that every text
 * missing it fails to compile.
 */

#pragma once

#include <ff_version.h>

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif


/** @brief Opaque engine instance (see ff.h). */
typedef struct ff ff_t;

/**
 * @def FF_FORMAT_ARG(i)
 * @brief Compiler hint that the function returns the @p i-th argument,
 *        or a text derived from it, as a printf format — so the format
 *        is checked against the arguments of the printf-style call it
 *        is passed to, as gettext() is. Vanishes on compilers without the
 *        attribute.
 */
#if defined(__GNUC__) || defined(__clang__)
#  define FF_FORMAT_ARG(i) __attribute__((format_arg(i)))
#else
#  define FF_FORMAT_ARG(i)
#endif

/** @brief The byte that starts a translation's tag (ASCII RS). */
#define FF_TR_SEP '\x1e'

/** @brief Most translations one text can have; FF_TR and the word
 *         tables take one to this many. Raising it means one more
 *         FF_TR_JOIN_n, FF_TR_CALL_n and ff_tr() parameter. */
#define FF_TR_MAX 2


/* Which languages a build keeps. Normally every one: a marker tags its
   literal. With FF_WITH_TR off only English is kept; in a check build
   only the language FF_TR_ONLY names. With one language kept a marker is
   its bare literal, or nothing. */
#if !FF_WITH_TR
#  define FF_TR_ONE_      1
#  define FF_TR_KEEP_EN_  1
#elif defined(FF_TR_ONLY_RU)
#  define FF_TR_ONE_      1
#  define FF_TR_KEEP_RU_  1
#endif

/** @brief A tag: FF_TR_SEP and a two-letter language code. */
#define FF_TR_TAG_(code)  "\x1e" code

#if !defined(FF_TR_ONE_)
/** @brief Marks @p s, a string literal, as English. */
#  define FF_TR_EN(s)  FF_TR_TAG_("en") s
/** @brief Marks @p s, a string literal, as Russian. */
#  define FF_TR_RU(s)  FF_TR_TAG_("ru") s
#else
#  if defined(FF_TR_KEEP_EN_)
#    define FF_TR_EN(s)  s
#  else
#    define FF_TR_EN(s)
#  endif
#  if defined(FF_TR_KEEP_RU_)
#    define FF_TR_RU(s)  s
#  else
#    define FF_TR_RU(s)
#  endif
#endif


/* Counting 1..FF_TR_MAX translations, for the macros below. EXPAND_ is
   for MSVC's traditional preprocessor (C++ hosts), which would pass
   __VA_ARGS__ on as one argument. */
#define FF_TR_EXPAND_(x)             x
#define FF_TR_CAT_(a, b)             FF_TR_CAT2_(a, b)
#define FF_TR_CAT2_(a, b)            a ## b
#define FF_TR_COUNT_(...)            FF_TR_EXPAND_(FF_TR_COUNT2_(__VA_ARGS__, 2, 1, 0))
#define FF_TR_COUNT2_(a, b, n, ...)  n

/**
 * @brief The translations, 1 to FF_TR_MAX of them, written next to each
 *        other: one literal holding every language, the form a text kept
 *        for later takes (see ff_tr_copy()). The word tables and
 *        FF_NATIVE() pass their manuals through it.
 */
#define FF_TR_JOIN(...) \
    FF_TR_EXPAND_(FF_TR_CAT_(FF_TR_JOIN_, FF_TR_COUNT_(__VA_ARGS__))(__VA_ARGS__))
#define FF_TR_JOIN_1(a)     a
#define FF_TR_JOIN_2(a, b)  a b

/** @brief An empty slot of ff_tr(): a tag no language has, and no text. */
#define FF_TR_NONE_  FF_TR_TAG_("--")

/**
 * @def FF_TR_LANG(lang, ...)
 * @brief The text, of the translations given (1 to FF_TR_MAX), in the
 *        language @p lang, a code or locale name; else the English one.
 *        For code with no engine at hand (FF_TR takes the engine's).
 *        Where one language is kept, the literal itself.
 */
#if defined(FF_TR_ONE_)
#  define FF_TR_LANG(lang, ...)  (FF_TR_JOIN(__VA_ARGS__))
#else
#  define FF_TR_LANG(lang, ...) \
       FF_TR_EXPAND_(FF_TR_CAT_(FF_TR_CALL_, FF_TR_COUNT_(__VA_ARGS__))((lang), __VA_ARGS__))
#  define FF_TR_CALL_1(l, a)     ff_tr(l, a, FF_TR_NONE_)
#  define FF_TR_CALL_2(l, a, b)  ff_tr(l, a, b)
#endif

/**
 * @brief The text, of the translations given (1 to FF_TR_MAX), in the
 *        language of engine @p ff, else the English one. A text written
 *        at the place it is shown; for one kept for later, see
 *        ff_tr_copy().
 */
#define FF_TR(ff, ...)  FF_TR_LANG(ff_lang(ff), __VA_ARGS__)


/**
 * Pick a text by language — what FF_TR and FF_TR_LANG call; code seldom
 * calls it directly.
 *
 * @param lang A language code or locale name ("ru", "ru_RU.UTF-8"); NULL
 *             is English.
 * @param tr1  A translation, tagged by its marker; NULL, or one with no
 *             text, is skipped.
 * @param tr2  Another, as @p tr1.
 * @return The text in @p lang, without its tag; else the English one;
 *         else the first there is; "" when none has a text.
 */
const char *ff_tr(const char *lang, const char *tr1, const char *tr2)
    FF_FORMAT_ARG(2) FF_FORMAT_ARG(3);

/**
 * Copy the text in @p lang out of a text kept for later — the markers
 * written next to each other, as a word table's manual is: whatever
 * precedes the first tag (the same in every language), then the text in
 * @p lang, else the English one, else the first. A text with no tag is
 * copied whole.
 *
 * snprintf-shaped: always NUL-terminates when @p size > 0, truncates at
 * @p size - 1 bytes, and returns the length the whole text needs,
 * excluding the NUL.
 *
 * @param lang A language code or locale name; NULL is English.
 * @param text The stored text; NULL is "".
 * @param buf  Destination; may be NULL when @p size is 0.
 * @param size Capacity of @p buf in bytes.
 * @return Bytes the text needs, excluding the NUL.
 */
size_t ff_tr_copy(const char *lang, const char *text, char *buf, size_t size);

/**
 * @return The language engine @p ff shows its texts in: a two-letter
 *         code, "en" unless set (always "en" when FF_WITH_TR is off).
 *         Owned by the engine.
 * @param ff Engine instance.
 */
const char *ff_lang(const ff_t *ff);

/**
 * Set the language engine @p ff shows its texts in, from the next one on
 * — messages raised and `man` entries shown after the call. Safe while a
 * native word runs; not from another thread or a signal handler.
 *
 * @param ff   Engine instance.
 * @param lang A two-letter ISO 639-1 code or a locale name that starts
 *             with one ("ru", "ru_RU.UTF-8", "ru-RU"). NULL, "C",
 *             "POSIX" and anything else are English. A language ff has
 *             no texts in is kept — a host's own texts may have it — and
 *             ff's texts show in English. A no-op when FF_WITH_TR is off.
 */
void ff_set_lang(ff_t *ff, const char *lang);

/**
 * The language a locale name names, if ff has texts in it: for a host
 * that picks the first language ff speaks from a list, such as
 * `$LANGUAGE`.
 *
 * @param locale A two-letter code or a locale name ("ru_RU.UTF-8").
 * @return Its code ("ru"), a static string; NULL if ff has no texts in
 *         that language, or @p locale names none ("C", "POSIX", NULL).
 */
const char *ff_tr_lang_of(const char *locale);


#ifdef __cplusplus
}
#endif
