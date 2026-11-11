#!/bin/sh
# Produce the distributable archives on a macOS host.
set -eu

if [ "$#" -ne 1 ]; then
    echo "Usage: $0 OUTPUT_DIRECTORY" >&2
    exit 2
fi

out=$1
prefix="Linewren-$(cat VERSION)"
classic_archive="$prefix-Mac-OS-9.zip"
mkdir -p "$out"
for file in "$classic_archive" "$prefix-PowerPC-Tiger.dmg" "$prefix-PowerPC-Leopard.dmg"; do
    if [ -e "$out/$file" ]; then
        echo "Already exists: $out/$file" >&2
        exit 1
    fi
done

classic=$(nix build .#distribution --no-link --print-out-paths)
temp=$(mktemp -d "${TMPDIR:-/tmp}/linewren-release.XXXXXX")
trap 'rm -rf "$temp"' EXIT

cp "$classic/$classic_archive" "$temp/"
unzip -t "$temp/$classic_archive"

package_carbon() {
    build=$(nix build ".#$1" --no-link --print-out-paths)
    image="$temp/$prefix-PowerPC-$2.dmg"
    hdiutil convert "$build/Linewren.img" -format UDRO -o "$image"
    hdiutil verify "$image"
}

package_carbon powerpc-osx Tiger
package_carbon powerpc-osx-leopard Leopard
mv "$temp"/Linewren-* "$out/"
