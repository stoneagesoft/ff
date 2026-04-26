#!/bin/bash
# Build the libff1 / libff-dev / libff-doc Debian packages from the
# current tree. Run from the repository root. Resulting .deb files
# land in the parent directory next to the source tree.

set -euo pipefail

cd "$(dirname "$0")"

debuild \
    --no-lintian \
    --no-tgz-check \
    -e PATH \
    -b -uc -us -j"$(nproc)"
