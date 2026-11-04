#!/bin/sh
# Builds the Ubuntu package in an ubuntu:latest container, which is the latest LTS. Packages land in build/packages.
set -eu
cd "$(dirname "$0")/.."

version=$(sed -n 's/^project(LjDict VERSION \([^ ]*\).*/\1/p' CMakeLists.txt)
if ! head -1 debian/changelog | grep -q "^ljdict ($version)"; then
    echo "debian/changelog does not start with version $version" >&2
    exit 1
fi

mkdir -p build/packages
docker run --rm -v "$PWD:/src:ro" -v "$PWD/build/packages:/out" ubuntu:latest sh -euc "
    export DEBIAN_FRONTEND=noninteractive
    apt-get update -qq
    apt-get install -y -qq --no-install-recommends build-essential >/dev/null
    mkdir -p /work/ljdict-$version
    tar -C /src --exclude=./build -cf - . | tar -C /work/ljdict-$version -xf -
    cd /work/ljdict-$version
    apt-get build-dep -y -qq --no-install-recommends . >/dev/null
    dpkg-buildpackage -b -us -uc
    cp ../*.deb /out
"
ls build/packages/*.deb
