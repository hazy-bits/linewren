#!/usr/bin/env bash
set -euo pipefail

config_file=/etc/nix/nix.custom.conf
daemon_label=system/systems.determinate.nix-daemon
cache_url=https://autc04.cachix.org
cache_key='autc04.cachix.org-1:MyPhhbTlMACU1jhpdXlPlU4R4gyDEeNbuVYi/dU/2iU='

if ! grep -Fq "extra-trusted-substituters = $cache_url" "$config_file"; then
  printf '\n%s\n%s\n%s\n' \
    "extra-substituters = $cache_url" \
    "extra-trusted-substituters = $cache_url" \
    "extra-trusted-public-keys = $cache_key" \
    | sudo tee -a "$config_file" >/dev/null
fi

sudo launchctl kickstart -k "$daemon_label"

echo "Retro68 cache trust installed."
echo "Verify with: nix build .#powerpc --dry-run"
