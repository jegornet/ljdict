#!/bin/sh
# Builds build/LjDict.dmg: the release bundle with Homebrew's libraries copied
# into Contents/Frameworks, so it runs on a Mac without Homebrew.
set -eu
cd "$(dirname "$0")/.."

# Plain clang may resolve to a swiftly toolchain, so the compiler is named explicitly.
cmake -S . -B build -G Ninja -DCMAKE_CXX_COMPILER=/usr/bin/clang++ -DCMAKE_OBJCXX_COMPILER=/usr/bin/clang++
cmake --build build

version=$(/usr/libexec/PlistBuddy -c "Print CFBundleShortVersionString" build/LjDict.app/Contents/Info.plist)
stage=build/dmg
app=$stage/LjDict.app
frameworks=$app/Contents/Frameworks
rm -rf "$stage" build/LjDict.dmg
mkdir -p "$stage"
cp -R build/LjDict.app "$app"
mkdir -p "$frameworks"

# Prints the non-system libraries a Mach-O file links, as absolute paths that
# keep the name they are linked by (often a symlink to a longer versioned name).
deps() {
    dir=$(dirname "$(realpath "$1")")
    otool -L "$1" | tail -n +2 | awk '{print $1}' | while read -r dep; do
        case $dep in
            /usr/lib/* | /System/*) ;;
            @loader_path/*) echo "$dir/${dep#@loader_path/}" ;;
            @rpath/*) echo "$dir/${dep#@rpath/}" ;;
            @*) echo "unresolved $dep in $1" >&2; exit 1 ;;
            *) echo "$dep" ;;
        esac
    done
}

# Points every non-system reference of a Mach-O file at @rpath/<name>.
relink() {
    otool -L "$1" | tail -n +2 | awk '{print $1}' | while read -r dep; do
        case $dep in
            /usr/lib/* | /System/* | @rpath/*) ;;
            *) install_name_tool -change "$dep" "@rpath/$(basename "$dep")" "$1" 2>/dev/null ;;
        esac
    done
}

queue=$(deps "$app/Contents/MacOS/LjDict")
while [ -n "$queue" ]; do
    next=""
    for lib in $queue; do
        copy=$frameworks/$(basename "$lib")
        [ -e "$copy" ] && continue
        cp "$lib" "$copy"
        chmod u+w "$copy"
        next="$next $(deps "$lib")"
    done
    queue=$next
done

for lib in "$frameworks"/*.dylib; do
    install_name_tool -id "@rpath/$(basename "$lib")" "$lib" 2>/dev/null
    relink "$lib"
    install_name_tool -add_rpath @loader_path "$lib" 2>/dev/null || true
done
relink "$app/Contents/MacOS/LjDict"
install_name_tool -add_rpath @executable_path/../Frameworks "$app/Contents/MacOS/LjDict" 2>/dev/null

# Fail rather than ship a bundle that still reaches into Homebrew.
if otool -L "$app/Contents/MacOS/LjDict" "$frameworks"/*.dylib | grep -E '^\s+/(opt|usr/local)/'; then
    echo "Homebrew references left in the bundle" >&2
    exit 1
fi

codesign --force --sign - "$frameworks"/*.dylib 2>/dev/null
codesign --force --sign - "$app" 2>/dev/null
codesign --verify --deep --strict "$app"

ln -s /Applications "$stage/Applications"
hdiutil create -quiet -volname "LjDict $version" -srcfolder "$stage" -format UDZO -fs HFS+ build/LjDict.dmg
echo "Built build/LjDict.dmg"
