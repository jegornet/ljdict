#!/bin/sh
# Builds the Fedora package in a fedora:latest container. Packages land in build/packages.
set -eu
cd "$(dirname "$0")/.."

version=$(sed -n 's/^project(LjDict VERSION \([^ ]*\).*/\1/p' CMakeLists.txt)
if ! grep -q "^Version: *$version$" packaging/fedora/ljdict.spec; then
    echo "packaging/fedora/ljdict.spec does not say Version: $version" >&2
    exit 1
fi

mkdir -p build/packages
docker run --rm -v "$PWD:/src:ro" -v "$PWD/build/packages:/out" fedora:latest sh -euc "
    dnf install -y -q rpm-build 'dnf-command(builddep)'
    dnf builddep -y -q /src/packaging/fedora/ljdict.spec
    mkdir -p ~/rpmbuild/SOURCES
    tar -C /src --exclude=./build --transform 's,^\.,ljdict-$version,' -czf ~/rpmbuild/SOURCES/ljdict-$version.tar.gz .
    rpmbuild -ba /src/packaging/fedora/ljdict.spec
    cp ~/rpmbuild/RPMS/*/*.rpm ~/rpmbuild/SRPMS/*.rpm /out
"
ls build/packages/*.rpm
