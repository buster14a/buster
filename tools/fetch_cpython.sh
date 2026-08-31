#!/bin/sh
# Fetch the pinned CPython checkout the test_cpython harness verifies against.
# Upstream sources are never copied into or patched in this repository: the
# harness takes this checkout as an external path and requires it pristine.
#
# usage: tools/fetch_cpython.sh /path/to/cpython-v3.13.9
set -eu

TAG=v3.13.9
COMMIT=8183fa5e3f78ca6ab862de7fb8b14f3d929421e0

if [ "$#" -ne 1 ]; then
    echo "usage: $0 /path/to/cpython-$TAG" >&2
    exit 2
fi
destination=$1

if [ -e "$destination" ]; then
    echo "error: $destination already exists; refusing to touch it" >&2
    exit 1
fi

git clone --depth 1 --branch "$TAG" https://github.com/python/cpython "$destination"
actual=$(git -C "$destination" rev-parse HEAD)
if [ "$actual" != "$COMMIT" ]; then
    echo "error: tag $TAG resolved to $actual; the harness pins $COMMIT" >&2
    exit 1
fi
echo "CPYTHON_FETCH tag=$TAG commit=$COMMIT path=$destination"
