@echo off
rem ---------------------------------------------------------------------
rem ff Windows clean script. Mirrors clean.sh: wipes the CMake build
rem directory's contents (preserving the tracked build\.empty
rem placeholder), generated documentation, the per-example build dirs
rem under examples\, and runtime droppings such as ffsh\history.ff.
rem
rem .deb-packaging artefacts under debian\ are skipped — building Debian
rem packages from Windows isn't a supported workflow.
rem ---------------------------------------------------------------------

setlocal enabledelayedexpansion

set "REPO_ROOT=%~dp0"
if "%REPO_ROOT:~-1%"=="\" set "REPO_ROOT=%REPO_ROOT:~0,-1%"

rem -----------------------------------------------------------------
rem build\ contents. The directory itself is git-tracked (carries a
rem .empty placeholder so it exists on fresh clones), so we delete
rem children rather than the directory itself.
rem -----------------------------------------------------------------
if exist "%REPO_ROOT%\build" (
    pushd "%REPO_ROOT%\build" >nul
    for /f "delims=" %%f in ('dir /b /a-d 2^>nul') do (
        if /i not "%%f"==".empty" del /q "%%f" 2>nul
    )
    for /f "delims=" %%d in ('dir /b /ad 2^>nul') do rd /s /q "%%d" 2>nul
    popd >nul
)

rem -----------------------------------------------------------------
rem Generated documentation. The doc\Makefile clean target is the
rem authoritative list when GNU make is on PATH; otherwise fall back
rem to a hand-rolled removal of the same files.
rem -----------------------------------------------------------------
where make >nul 2>nul
if not errorlevel 1 (
    if exist "%REPO_ROOT%\doc\Makefile" (
        pushd "%REPO_ROOT%\doc" >nul
        make clean >nul 2>nul
        popd >nul
    )
) else (
    if exist "%REPO_ROOT%\doc\html"   rd /s /q "%REPO_ROOT%\doc\html"
    if exist "%REPO_ROOT%\doc\latex"  rd /s /q "%REPO_ROOT%\doc\latex"
    del /q "%REPO_ROOT%\doc\ff-*.pdf"           2>nul
    del /q "%REPO_ROOT%\doc\doxy.log"           2>nul
    del /q "%REPO_ROOT%\doc\last-modified.tex"  2>nul
    del /q "%REPO_ROOT%\doc\md\01-changelog.md" 2>nul
)

rem -----------------------------------------------------------------
rem Per-example build dirs and runtime artefacts.
rem -----------------------------------------------------------------
for /d %%d in ("%REPO_ROOT%\examples\*") do (
    if exist "%%d\build" rd /s /q "%%d\build"
)
del /q "%REPO_ROOT%\examples\ffsh\history.ff" 2>nul

endlocal
