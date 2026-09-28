#ifndef FF_IN_EXEC
#error "This is a dispatch fragment #included inside ff_exec(); do not include it directly."
#endif

/*
 * ff --- file I/O word dispatch cases.
 *
 * This header is included inside the `switch (*ip++)` in ff_exec().
 * It is NOT a standalone header — don't include it elsewhere.
 *
 * Each external libc call is wrapped in _FF_SYNC()/_FF_RESTORE() so the
 * cached TOS is materialized in memory across the call boundary in case
 * the call path touches ff state through `ff` itself.
 *
 * Every word here that reaches outside the engine first checks that the
 * host hasn't withheld it (ff_platform::deny), and builds configured
 * without FF_WITH_SYSTEM / FF_WITH_FILES leave them out.
 */

#if FF_WITH_SYSTEM
/** ( cmd -- ec )  `system` — run cmd through the host's command runner
    (ff_platform::run_command), or the C library's system(). */
case FF_OP_SYSTEM:
    _FF_NEED_CAP(FF_CAP_SYSTEM, "system");
    _FF_SL(1);
    _FF_CHECK_STR(tos);
    _FF_SYNC();
    {
        const char *cmd = (const char *)(intptr_t)tos;
        tos = ff->platform.run_command
                  ? ff->platform.run_command(ff->platform.context, cmd)
                  : system(cmd);
    }
    _FF_NEXT();
#endif

#if FF_WITH_FILES
/** ( mode path -- fp )  `fopen` — open a file, through the host's
    ff_platform::open_file if it has one; an error on failure. */
case FF_OP_FOPEN:
    _FF_NEED_CAP(FF_CAP_FILES, "fopen");
    _FF_SL(2);
    _FF_CHECK_STR(tos);
    _FF_CHECK_STR(_FF_NOS);
    _FF_SYNC();
    {
        int slot = ff_file_slot(ff, NULL);
        if (slot < 0)
        {
            ff_tracef(ff, FF_SEV_ERROR | FF_ERR_FILE_IO,
                      "Too many open files (%d).", FF_OPEN_FILES_MAX);
            goto done;
        }
        FILE *f = ff_open_file(ff, (const char *)(intptr_t)tos,
                               (const char *)(intptr_t)_FF_NOS);
        if (!f)
        {
            ff_tracef(ff, FF_SEV_ERROR | FF_ERR_FILE_IO,
                      "Failed to open file '%s': %s.",
                      (const char *)(intptr_t)tos, strerror(errno));
            goto done;
        }
        ff->files[slot] = f;
        --S->top;
        tos = (ff_int_t)(intptr_t)f;
    }
    _FF_NEXT();

/** ( fp -- ec )  `fclose` — close a file the program opened. */
case FF_OP_FCLOSE:
    _FF_NEED_CAP(FF_CAP_FILES, "fclose");
    _FF_SL(1);
    _FF_CHECK_FILE(tos, false);
    _FF_SYNC();
    {
        FILE *f = (FILE *)(intptr_t)tos;
        int slot = ff_file_slot(ff, f);
        if (slot >= 0)
            ff->files[slot] = NULL;
        tos = fclose(f);
    }
    _FF_NEXT();

/** ( fp size buf -- result )  `fgets` — read line into buf. */
case FF_OP_FGETS:
    _FF_NEED_CAP(FF_CAP_FILES, "fgets");
    _FF_SL(3);
    /* Unsigned, so the upper bound isn't a tautology with 32-bit cells. */
    _FF_BAD_SIZE(_FF_NOS < 1 || (ff_uint_t)_FF_NOS > (ff_uint_t)INT_MAX,
                 "fgets", _FF_NOS);
    _FF_CHECK_WRITE((void *)(intptr_t)tos, (size_t)_FF_NOS);
    _FF_CHECK_FILE(_FF_SAT(2), true);
    _FF_SYNC();
    {
        char *r = fgets((char *)(intptr_t)tos, (int)_FF_NOS,
                        (FILE *)(intptr_t)_FF_SAT(2));
        S->top -= 2;
        tos = (ff_int_t)(intptr_t)r;
    }
    _FF_NEXT();

/** ( fp s -- result )  `fputs` — write a string. */
case FF_OP_FPUTS:
    _FF_NEED_CAP(FF_CAP_FILES, "fputs");
    _FF_SL(2);
    _FF_CHECK_STR(tos);
    _FF_CHECK_FILE(_FF_NOS, true);
    _FF_SYNC();
    {
        int r = fputs((const char *)(intptr_t)tos,
                      (FILE *)(intptr_t)_FF_NOS);
        --S->top;
        tos = r;
    }
    _FF_NEXT();

/** ( fp -- ch )  `fgetc` — read a single byte. */
case FF_OP_FGETC:
    _FF_NEED_CAP(FF_CAP_FILES, "fgetc");
    _FF_SL(1);
    _FF_CHECK_FILE(tos, true);
    _FF_SYNC();
    tos = fgetc((FILE *)(intptr_t)tos);
    _FF_NEXT();

/** ( fp ch -- result )  `fputc` — write a single byte. */
case FF_OP_FPUTC:
    _FF_NEED_CAP(FF_CAP_FILES, "fputc");
    _FF_SL(2);
    _FF_CHECK_FILE(_FF_NOS, true);
    _FF_SYNC();
    {
        int r = fputc((int)tos, (FILE *)(intptr_t)_FF_NOS);
        --S->top;
        tos = r;
    }
    _FF_NEXT();

/** ( fp -- pos )  `ftell` — current file position. */
case FF_OP_FTELL:
    _FF_NEED_CAP(FF_CAP_FILES, "ftell");
    _FF_SL(1);
    _FF_CHECK_FILE(tos, true);
    _FF_SYNC();
    tos = ftell((FILE *)(intptr_t)tos);
    _FF_NEXT();

/** ( fp off whence -- result )  `fseek` — reposition fp. */
case FF_OP_FSEEK:
    _FF_NEED_CAP(FF_CAP_FILES, "fseek");
    _FF_SL(3);
    _FF_CHECK_FILE(_FF_SAT(2), true);
    _FF_SYNC();
    {
        int r = fseek((FILE *)(intptr_t)_FF_SAT(2),
                      (long)_FF_NOS, (int)tos);
        S->top -= 2;
        tos = r;
    }
    _FF_NEXT();

/** ( -- fp )  `stdin` — push the standard input file pointer. */
case FF_OP_STDIN:
    _FF_NEED_CAP(FF_CAP_FILES, "stdin");
    _FF_SO(1);
    _FF_PUSH_PTR(stdin);
    _FF_NEXT();

/** ( -- fp )  `stdout` — push the standard output file pointer. */
case FF_OP_STDOUT:
    _FF_NEED_CAP(FF_CAP_FILES, "stdout");
    _FF_SO(1);
    _FF_PUSH_PTR(stdout);
    _FF_NEXT();

/** ( -- fp )  `stderr` — push the standard error file pointer. */
case FF_OP_STDERR:
    _FF_NEED_CAP(FF_CAP_FILES, "stderr");
    _FF_SO(1);
    _FF_PUSH_PTR(stderr);
    _FF_NEXT();

/** ( -- whence )  `seek_set` — push SEEK_SET. */
case FF_OP_SEEK_SET:
    _FF_SO(1);
    _FF_PUSH(SEEK_SET);
    _FF_NEXT();

/** ( -- whence )  `seek_cur` — push SEEK_CUR. */
case FF_OP_SEEK_CUR:
    _FF_SO(1);
    _FF_PUSH(SEEK_CUR);
    _FF_NEXT();

/** ( -- whence )  `seek_end` — push SEEK_END. */
case FF_OP_SEEK_END:
    _FF_SO(1);
    _FF_PUSH(SEEK_END);
    _FF_NEXT();
#endif

/** ( errno -- s )  `strerror` — translate errno into a message pointer. */
case FF_OP_STRERROR:
    _FF_SL(1);
#if FF_SAFE_MEM
    /* The C library's buffer isn't memory the checks know; hand out a
       copy in the string arena, which they do. */
    _FF_SYNC();
    {
        const char *msg = strerror((int)tos);
        char *copy = ff_pad_intern(ff, msg, strlen(msg));
        if (!copy)
        {
            ff_mem_check(ff);
            goto done;
        }
        tos = (ff_int_t)(intptr_t)copy;
    }
#else
    tos = (ff_int_t)(intptr_t)strerror((int)tos);
#endif
    _FF_NEXT();
