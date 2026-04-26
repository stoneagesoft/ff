/*
 * ff --- file I/O word dispatch cases.
 *
 * This header is included inside the `switch (*ip++)` in ff_exec().
 * It is NOT a standalone header — don't include it elsewhere.
 *
 * Each external libc call is wrapped in _FF_SYNC()/_FF_RESTORE() so the
 * cached TOS is materialized in memory across the call boundary in case
 * the call path touches ff state through `ff` itself.
 */

/** ( cmd -- ec )  `system` — pass cmd to the C library system(). */
case FF_OP_SYSTEM:
    _FF_SL(1);
    _FF_SYNC();
    tos = system((const char *)(intptr_t)tos);
    _FF_NEXT();

/** ( mode path -- fp )  `fopen` — open a file; aborts on failure. */
case FF_OP_FOPEN:
    _FF_SL(2);
    _FF_SYNC();
    {
        FILE *f = fopen((const char *)(intptr_t)tos,
                        (const char *)(intptr_t)_NOS);
        if (!f)
        {
            ff_tracef(ff, FF_SEV_ERROR | FF_ERR_FILE_IO,
                      "Failed to open file '%s': %s.",
                      (const char *)(intptr_t)tos, strerror(errno));
            goto done;
        }
        --S->top;
        tos = (ff_int_t)(intptr_t)f;
    }
    _FF_NEXT();

/** ( fp -- ec )  `fclose` — close a file. */
case FF_OP_FCLOSE:
    _FF_SL(1);
    _FF_SYNC();
    tos = fclose((FILE *)(intptr_t)tos);
    _FF_NEXT();

/** ( fp size buf -- result )  `fgets` — read line into buf. */
case FF_OP_FGETS:
    _FF_SL(3);
    _FF_SYNC();
    {
        char *r = fgets((char *)(intptr_t)tos, (int)_NOS,
                        (FILE *)(intptr_t)_SAT(2));
        S->top -= 2;
        tos = (ff_int_t)(intptr_t)r;
    }
    _FF_NEXT();

/** ( fp s -- result )  `fputs` — write a string. */
case FF_OP_FPUTS:
    _FF_SL(2);
    _FF_SYNC();
    {
        int r = fputs((const char *)(intptr_t)tos,
                      (FILE *)(intptr_t)_NOS);
        --S->top;
        tos = r;
    }
    _FF_NEXT();

/** ( fp -- ch )  `fgetc` — read a single byte. */
case FF_OP_FGETC:
    _FF_SL(1);
    _FF_SYNC();
    tos = fgetc((FILE *)(intptr_t)tos);
    _FF_NEXT();

/** ( fp ch -- result )  `fputc` — write a single byte. */
case FF_OP_FPUTC:
    _FF_SL(2);
    _FF_SYNC();
    {
        int r = fputc((int)tos, (FILE *)(intptr_t)_NOS);
        --S->top;
        tos = r;
    }
    _FF_NEXT();

/** ( fp -- pos )  `ftell` — current file position. */
case FF_OP_FTELL:
    _FF_SL(1);
    _FF_SYNC();
    tos = ftell((FILE *)(intptr_t)tos);
    _FF_NEXT();

/** ( fp off whence -- result )  `fseek` — reposition fp. */
case FF_OP_FSEEK:
    _FF_SL(3);
    _FF_SYNC();
    {
        int r = fseek((FILE *)(intptr_t)_SAT(2),
                      (long)_NOS, (int)tos);
        S->top -= 2;
        tos = r;
    }
    _FF_NEXT();

/** ( -- fp )  `stdin` — push the standard input file pointer. */
case FF_OP_STDIN:
    _FF_SO(1);
    _PUSH_PTR(stdin);
    _FF_NEXT();

/** ( -- fp )  `stdout` — push the standard output file pointer. */
case FF_OP_STDOUT:
    _FF_SO(1);
    _PUSH_PTR(stdout);
    _FF_NEXT();

/** ( -- fp )  `stderr` — push the standard error file pointer. */
case FF_OP_STDERR:
    _FF_SO(1);
    _PUSH_PTR(stderr);
    _FF_NEXT();

/** ( -- whence )  `seek_set` — push SEEK_SET. */
case FF_OP_SEEK_SET:
    _FF_SO(1);
    _PUSH(SEEK_SET);
    _FF_NEXT();

/** ( -- whence )  `seek_cur` — push SEEK_CUR. */
case FF_OP_SEEK_CUR:
    _FF_SO(1);
    _PUSH(SEEK_CUR);
    _FF_NEXT();

/** ( -- whence )  `seek_end` — push SEEK_END. */
case FF_OP_SEEK_END:
    _FF_SO(1);
    _PUSH(SEEK_END);
    _FF_NEXT();

/** ( errno -- s )  `strerror` — translate errno into a message pointer. */
case FF_OP_STRERROR:
    _FF_SL(1);
    tos = (ff_int_t)(intptr_t)strerror((int)tos);
    _FF_NEXT();
