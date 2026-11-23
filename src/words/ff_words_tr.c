/*
 * ff --- words for the language of ff's texts.
 */

#include <ff_p.h>
#include <ff_word_def_p.h>

#include <stdio.h>
#include <string.h>


/* ===================================================================
 * Language words
 * =================================================================== */

void ff_w_lang_fetch_impl(ff_t *ff)
{
    /* A copy in the string arena: the engine's own field is no tracked
       region (FF_SAFE_MEM would refuse it to `type`), and a program
       storing into it would change the language under the engine. */
    const char *lang = ff_lang(ff);
    ff_push_str(ff, lang, strlen(lang));
}

void ff_w_lang_store_impl(ff_t *ff)
{
    const char *s;
    if (!ff_pop_str(ff, &s))
        return;
    if (!ff_set_lang(ff, s))
        ff_throwf(ff, FF_THROW_BAD_ARG,
                  FF_TR(ff, FF_TR_EN("lang!: '%s' names no language."),
                            FF_TR_RU("lang!: «%s» не называет языка.")), s);
}

void ff_w_dot_langs_impl(ff_t *ff)
{
    const char *current = ff_lang(ff);
    for (const char *const *lang = ff_tr_langs(); *lang; ++lang)
        ff_printf(ff, "%c %s  %s\n", strcmp(*lang, current) == 0 ? '*' : ' ',
                  *lang, ff_tr_lang_name(*lang));
}


const ff_word_def_t FF_TR_WORDS[] =
{
    _FF_W("lang@", FF_OP_LANG_FETCH, "( -- s )",
      FF_TR_EN("Current language\n"
               "Leaves the code of the language ff shows its texts in — messages,\n"
               "**man** entries — as a string: `en` or `ru`. The string is a copy:\n"
               "storing into it changes nothing.\n"
               "\n"
               "See also: **lang!**, **.langs**"),
      FF_TR_RU("Текущий язык\n"
               "Оставляет строкой код языка, на котором ff показывает свои тексты, —\n"
               "сообщения, справку **man**: `en` или `ru`. Строка — копия: запись\n"
               "в неё ничего не меняет.\n"
               "\n"
               "См. также: **lang!**, **.langs**")),
    _FF_W("lang!", FF_OP_LANG_STORE, "( s -- )",
      FF_TR_EN("Set language\n"
               "From now on ff shows its texts — messages, **man** entries — in the\n"
               "language *s* names: a code (`ru`) or a locale name (`ru_RU.UTF-8`).\n"
               "A language ff has no texts in gives English, which **lang@** then\n"
               "reports. A string that names no language is an error (-24).\n"
               "\n"
               "See also: **lang@**, **.langs**"),
      FF_TR_RU("Задать язык\n"
               "С этого момента ff показывает свои тексты — сообщения, справку\n"
               "**man** — на языке, который называет *s*: код (`ru`) или имя локали\n"
               "(`ru_RU.UTF-8`). Язык, на котором у ff нет текстов, даёт английский —\n"
               "его и вернёт **lang@**. Строка, не называющая языка, — ошибка (-24).\n"
               "\n"
               "См. также: **lang@**, **.langs**")),
    _FF_W(".langs", FF_OP_DOT_LANGS, "( -- )",
      FF_TR_EN("List languages\n"
               "Lists the languages ff has texts in, each with its code and its\n"
               "name in that language; the current one is marked.\n"
               "\n"
               "See also: **lang@**, **lang!**"),
      FF_TR_RU("Список языков\n"
               "Печатает языки, на которых у ff есть тексты, — код каждого и его\n"
               "название на нём самом; текущий отмечен.\n"
               "\n"
               "См. также: **lang@**, **lang!**")),
    FF_WEND
};
