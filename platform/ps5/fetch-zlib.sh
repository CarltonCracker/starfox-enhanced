#!/usr/bin/env bash
# Pre-fetch the exact upstream zlib archive with fail-closed transport retries.
# The pinned PS5_Vulkan bootstrap independently checks the same digest again.
set -euo pipefail
directory=${1:?Usage: fetch-zlib.sh PS5_Vulkan-directory}
version=1.3.2
digest=bb329a0a2cd0274d05519d61c667c062e06990d72e125ee2dfa8de64f0119d16
script="$directory/tools/setup-native-dependencies.sh"
grep -Fqx "zlib_version=\"$version\"" "$script" || {
    echo 'Pinned upstream zlib version changed; verify before building' >&2; exit 2;
}
grep -Fqx "zlib_hash=\"$digest\"" "$script" || {
    echo 'Pinned upstream zlib digest changed; verify before building' >&2; exit 2;
}
archive="$directory/.deps/native/zlib/zlib-$version.tar.gz"
mkdir -p -- "$(dirname -- "$archive")"
verified() { printf '%s  %s\n' "$digest" "$1" | sha256sum --check --strict >/dev/null 2>&1; }
if [[ -f $archive ]] && verified "$archive"; then exit 0; fi
# A proxy/error page may return HTTP 200. Never accept it or relax the hash.
# Retry the fixed-version official endpoints, not an unpinned current archive.
pending="$archive.verified-download"
trap 'rm -f -- "$pending"' EXIT
for endpoint in "https://zlib.net/zlib-$version.tar.gz" "https://zlib.net/fossils/zlib-$version.tar.gz"; do
    for attempt in 1 2 3; do
        if curl --fail --location --silent --show-error --connect-timeout 20 --max-time 120 \
            "$endpoint" --output "$pending" && verified "$pending"; then
            mv -- "$pending" "$archive"
            echo "==> Verified official zlib $version archive" >&2
            exit 0
        fi
        echo "zlib download/integrity check failed (attempt $attempt): $endpoint" >&2
        if [[ -f $pending ]]; then sha256sum "$pending" >&2; fi
        sleep 2
    done
done
echo 'No verified zlib archive; refusing to build PS5 dependencies' >&2
exit 1
