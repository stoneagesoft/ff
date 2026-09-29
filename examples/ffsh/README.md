# Fortissimo Shell

An interactive shell for *ff*, and the reference for embedding it.

```
ffsh              read Forth from stdin, a line at a time
ffsh FILE...      run the files in turn, as `load` does, and exit;
                  the status is non-zero if one fails
```

Errors, warnings and `trace` output go to stderr. Ctrl-C aborts the
running evaluation; with `FFSH_TIMEOUT_MS` set, so does the watchdog
once a line (or a file) has run that many milliseconds. Each line typed
is appended to `$XDG_DATA_HOME/ff/history.ff` (`~/.local/share/ff/` by
default, `%APPDATA%\ff\` on Windows); `FFSH_HISTORY` names another file.

Build it with the library (`cmake -B build -DFF_BUILD_EXAMPLES=ON`), or
on its own against an installed *ff*:

```sh
cmake -S examples/ffsh -B build/ffsh
cmake --build build/ffsh
```
