#!/bin/bash
# Restore a freshly-cloned tree state. Removes:
#   - the CMake build dir
#   - everything the doc Makefile produces (HTML, LaTeX, PDF, logs)
#   - everything dh / debhelper drops into debian/ (per-package staging
#     dirs, debhelper-build-stamp, files, *.substvars, *.debhelper.log,
#     .debhelper/ cache, debian/tmp)
#   - macOS metadata droppings (.DS_Store, ._*)
#
# Build outputs that land in the parent directory (../*.deb, ../*.ddeb,
# ../*.changes, ../*.buildinfo) are deliverables, not intermediates,
# and are left alone.

set -euo pipefail

cd "$(dirname "$0")"

# Defer to debian/rules clean for the bulk of the work — it knows the
# full set of artefacts the package build creates.
if [ -x debian/rules ]; then
    fakeroot debian/rules clean >/dev/null 2>&1 || true
fi

# Belt-and-braces removal in case dh clean wasn't available or bailed
# out early. `build/` is itself tracked (carries a `.empty` placeholder
# so the directory exists in fresh clones), so we wipe its contents
# rather than the directory itself.
find build -mindepth 1 ! -name .empty -delete 2>/dev/null || true

# Delegate to doc/Makefile's own `clean` target — it knows the full
# set of Doxygen / pandoc / pdflatex / plantuml outputs (HTML, PDF,
# EPUB, DOCX, LaTeX aux, plantuml-images, symbols.sql, the
# auto-generated changelog) and tracks new ones as the doc pipeline
# evolves.
if [ -f doc/Makefile ]; then
    make -C doc clean >/dev/null 2>&1 || true
fi

rm -rf debian/.debhelper debian/tmp
rm -rf debian/libff1 debian/libff-dev debian/libff-doc
rm -f  debian/debhelper-build-stamp debian/files
rm -f  debian/*.substvars debian/*.debhelper.log debian/*.debhelper

# Examples have their own build trees and runtime artefacts. Defer to
# the per-example clean script when one exists; otherwise wipe the
# build dir and any obvious runtime droppings.
for ex in examples/*/; do
    if [ -x "$ex/clean.sh" ]; then
        ( cd "$ex" && ./clean.sh )
    else
        rm -rf "$ex/build"
    fi
done
rm -f examples/ffsh/history.ff

# Stale macOS metadata occasionally checked into the tree.
find . -name .DS_Store -delete
find . -name '._*'      -delete

# Top-level stray logs from older build flows.
rm -f ./*.log
