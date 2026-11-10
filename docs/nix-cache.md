# Retro68 binary cache on macOS

Retro68 publishes current Nix build products to the `autc04` Cachix cache. With
a multi-user Determinate Nix installation, putting the cache only in
`~/.config/nix/nix.conf` is insufficient: the system daemon must trust it.

Add these lines to `/etc/nix/nix.custom.conf` (the supported local override file
included by Determinate Nix):

```nix
extra-substituters = https://autc04.cachix.org
extra-trusted-substituters = https://autc04.cachix.org
extra-trusted-public-keys = autc04.cachix.org-1:MyPhhbTlMACU1jhpdXlPlU4R4gyDEeNbuVYi/dU/2iU=
```

Then restart the Determinate Nix daemon:

```sh
sudo launchctl kickstart -k system/systems.determinate.nix-daemon
```

This repository includes an idempotent helper that performs both steps and
prompts for administrator authentication:

```sh
./scripts/configure-nix-cache.sh
```

Verify before starting a real build:

```sh
nix build .#powerpc --dry-run
```

The Retro68 compiler and tools should be listed under “will be fetched”, not
“will be built”. Stop and fix the cache configuration if Nix proposes building
`retro68.gcc_unwrapped`.

The cache contains current Retro68 revisions. Very old lock files may point at
store paths that have never been uploaded or are no longer retained, so keep
the Retro68 input reasonably current.
