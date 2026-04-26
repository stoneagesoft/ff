#!/bin/bash

### Console colors.

ECHO_ESCAPE=" -e "

TEXT_FG_BLACK="[30m"
TEXT_FG_RED="[31m"
TEXT_FG_GREEN="[32m"
TEXT_FG_ORANGE="[33m"
TEXT_FG_BLUE="[34m"
TEXT_FG_MAGENTA="[35m"
TEXT_FG_CYAN="[36m"
TEXT_FG_GRAY="[37m"
TEXT_FG_DARK_GRAY="[30;1m"
TEXT_FG_LIGHT_RED="[31;1m"
TEXT_FG_LIGHT_GREEN="[32;1m"
TEXT_FG_YELLOW="[33;1m"
TEXT_FG_VIOLET="[34;1m"
TEXT_FG_LIGHT_MAGENTA="[35;1m"
TEXT_FG_LIGHT_CYAN="[36;1m"
TEXT_FG_WHITE="[37;1m"

TEXT_BG_BLACK="[40m"
TEXT_BG_RED="[41m"
TEXT_BG_GREEN="[42m"
TEXT_BG_YELLOW="[43m"
TEXT_BG_BLUE="[44m"
TEXT_BG_MAGENTA="[45m"
TEXT_BG_CYAN="[46m"
TEXT_BG_GRAY="[47m"

TEXT_NORM="[0m" # Back to normal text

FFSH_MSG_PREFIX="$TEXT_BG_GREEN$TEXT_FG_YELLOW ffsh> $TEXT_NORM"

_banner()
{
    echo
    echo $ECHO_ESCAPE$TEXT_BG_GREEN$TEXT_FG_WHITE"┌─────────────"$TEXT_FG_WHITE"┐"$TEXT_NORM
    echo $ECHO_ESCAPE$TEXT_BG_GREEN$TEXT_FG_WHITE"│ "$TEXT_FG_YELLOW"Forth"$TEXT_FG_WHITE" Shell "$TEXT_FG_WHITE"│"$TEXT_NORM
    echo $ECHO_ESCAPE$TEXT_BG_GREEN$TEXT_FG_WHITE"└"$TEXT_FG_WHITE"─────────────┘"$TEXT_NORM
    echo
}

_comment()
{
    echo
    echo $ECHO_ESCAPE $TEXT_FG_YELLOW" $1 "$TEXT_NORM
    echo
}

_failed()
{
    echo
    echo $ECHO_ESCAPE$TT_MSG_PREFIX$TEXT_BG_RED$TEXT_FG_YELLOW" Failed :( "$TEXT_NORM
    echo
}

_completed()
{
    echo
    echo $ECHO_ESCAPE$TT_MSG_PREFIX$TEXT_BG_CYAN$TEXT_FG_WHITE" Completed successfully :) "$TEXT_NORM
    echo
}

_ex()
{
    echo $@
    if ! $@
    then
        _failed
        exit 1
    fi
}

clear

_banner

export FFSH_OS=`uname -s | tr '[:upper:]' '[:lower:]'`

echo $@ | grep -- 'release' > /dev/null 2>&1
if [ "$?" == "0" ]; then
    CMAKE_BUILD_TYPE=Release
    FFSH_BUILD_OPTS_HR=$FFSH_BUILD_OPTS_HR' release'
else
    CMAKE_BUILD_TYPE=Debug
    FFSH_BUILD_OPTS_HR=$FFSH_BUILD_OPTS_HR' debug'
fi

echo $@ | grep -- 'static' > /dev/null 2>&1
if [ "$?" == "0" ]; then
    FFSH_BUILD_OPTS=$FFSH_BUILD_OPTS' -DBUILD_SHARED=FALSE'
    FFSH_BUILD_OPTS_HR=$FFSH_BUILD_OPTS_HR' static'
else
    FFSH_BUILD_OPTS=$FFSH_BUILD_OPTS' -DBUILD_SHARED=TRUE'
    FFSH_BUILD_OPTS_HR=$FFSH_BUILD_OPTS_HR' shared'
fi

echo $@ | grep -- 'verbose' > /dev/null 2>&1
if [ "$?" == "0" ]; then
    CMAKE_VERBOSE_MAKEFILE=TRUE
    FFSH_BUILD_OPTS_HR=$FFSH_BUILD_OPTS_HR' verbose'
else
    CMAKE_VERBOSE_MAKEFILE=FALSE
    FFSH_BUILD_OPTS_HR=$FFSH_BUILD_OPTS_HR' silent'
fi

echo $@ | grep -- 'single' > /dev/null 2>&1
if [ "$?" -eq "0" ]; then
    FFSH_BUILD_THREADS=1
    FFSH_BUILD_OPTS_HR=$FFSH_BUILD_OPTS_HR' single-thread'
else
    FFSH_BUILD_OPTS_HR=$FFSH_BUILD_OPTS_HR' multi-threaded'
fi

echo $@ | grep -- 'wasm' > /dev/null 2>&1
if [ "$?" == "0" ]; then
    FFSH_BUILD_OPTS=$FFSH_BUILD_OPTS'-DFFSH_WASM=TRUE'
    FFSH_BUILD_OPTS_HR=$FFSH_BUILD_OPTS_HR' wasm'

fi

echo $FFSH_MSG_PREFIX" Build mode:$FFSH_BUILD_OPTS_HR"
if [ "$FFSH_BUILD_THREADS" != "" ]; then
    echo $FFSH_MSG_PREFIX" Jobs......: $FFSH_BUILD_THREADS"
fi
echo

cd build
_ex cmake -G"Ninja" -DCMAKE_BUILD_TYPE=$CMAKE_BUILD_TYPE $FFSH_BUILD_OPTS -DCMAKE_VERBOSE_MAKEFILE=$CMAKE_VERBOSE_MAKEFILE ..
_ex cmake --build . -j $FFSH_BUILD_THREADS
# _ex sudo cmake --install .

_completed
