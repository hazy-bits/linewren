#!/bin/sh
set -eu

cd "$(dirname "$0")/.."
test_dir=$(mktemp -d "${TMPDIR:-/tmp}/linewren-local-tests.XXXXXXXX")
trap 'rm -f "$test_dir/document-test"; rmdir "$test_dir"' EXIT
${CC:-cc} -std=c11 -Wall -Wextra -Werror -DMD4C_USE_UTF16 -I. \
    document.c markdown.c vendor/md4c/md4c.c tests/document-test.c \
    -o "$test_dir/document-test"
"$test_dir/document-test"
printf 'LINEWREN_LOCAL_TESTS_OK\n'
