/*
 * ff --- heap word dispatch cases.
 *
 * This header is included inside the `switch (*ip++)` in ff_exec().
 * It is NOT a standalone header — don't include it elsewhere.
 */

/** ( -- a )  `here` — push a pointer to the next free heap slot. */
_FF_CASE(FF_OP_HERE)
    _FF_SO(1);
    {
        ff_heap_t *h = &ff_dict_top(&ff->dict)->heap;
        ff_int_t *p = h->byte_off
                    ? (ff_int_t *)((char *)&h->data[h->size - 1] + h->byte_off)
                    : &h->data[h->size];
        _PUSH_PTR(p);
    }
    _FF_NEXT();

/** ( v a -- )  `!` — store v at address a. */
_FF_CASE(FF_OP_STORE)
    _FF_SL(2);
    _FF_CHECK_ADDR((const void *)(intptr_t)tos, sizeof(ff_int_t));
    *(ff_int_t *)(intptr_t)tos = _NOS;
    _DROPN(2);
    _FF_NEXT();

/** ( a -- v )  `@` — fetch from address a. */
_FF_CASE(FF_OP_FETCH)
    _FF_SL(1);
    _FF_CHECK_ADDR((const void *)(intptr_t)tos, sizeof(ff_int_t));
    tos = *(ff_int_t *)(intptr_t)tos;
    _FF_NEXT();

/** ( v a -- )  `+!` — *a += v. */
_FF_CASE(FF_OP_PLUS_STORE)
    _FF_SL(2);
    _FF_CHECK_ADDR((const void *)(intptr_t)tos, sizeof(ff_int_t));
    *(ff_int_t *)(intptr_t)tos += _NOS;
    _DROPN(2);
    _FF_NEXT();

/** ( n -- )  `allot` — reserve n cells in the current heap. */
_FF_CASE(FF_OP_ALLOT)
    _FF_SL(1);
    ff_heap_alloc(&ff_dict_top(&ff->dict)->heap, (int)tos);
    _DROP();
    _FF_NEXT();

/** ( v -- )  `,` — append a single cell to the current heap. */
_FF_CASE(FF_OP_COMMA)
    _FF_SL(1);
    ff_heap_compile_int(&ff_dict_top(&ff->dict)->heap, tos);
    _DROP();
    _FF_NEXT();

/** ( v a -- )  `c!` — store v as a single byte at address a. */
_FF_CASE(FF_OP_C_STORE)
    _FF_SL(2);
    _FF_CHECK_ADDR((const void *)(intptr_t)tos, sizeof(char));
    *(char *)(intptr_t)tos = (char)_NOS;
    _DROPN(2);
    _FF_NEXT();

/** ( a -- v )  `c@` — fetch a single byte from address a. */
_FF_CASE(FF_OP_C_FETCH)
    _FF_SL(1);
    _FF_CHECK_ADDR((const void *)(intptr_t)tos, sizeof(char));
    tos = *(unsigned char *)(intptr_t)tos;
    _FF_NEXT();

/** ( v -- )  `c,` — append a single byte to the current heap. */
_FF_CASE(FF_OP_C_COMMA)
    _FF_SL(1);
    ff_heap_compile_char(&ff_dict_top(&ff->dict)->heap, (char)tos);
    _DROP();
    _FF_NEXT();

/** ( -- )  `align` — align current heap to a cell boundary. */
_FF_CASE(FF_OP_C_ALIGN)
    ff_heap_align(&ff_dict_top(&ff->dict)->heap);
    _FF_NEXT();
