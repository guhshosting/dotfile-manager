# Debian packaging

Build a `.deb` package for dotfile-manager:

```sh
cd packaging/debian
./build-deb.sh
```

This builds a Release configuration (tests disabled, `DFM_BUILD_TESTS=OFF`)
under `../../build-pkg/`, stages `usr/bin/dotfile-manager`,
`usr/share/man/man1/dotfile-manager.1.gz`, and
`usr/share/doc/dotfile-manager/{README.md,copyright}`, then runs
`dpkg-deb --build` to produce `../dotfile-manager_1.0.0_<arch>.deb`.

Install with:

```sh
sudo dpkg -i ../dotfile-manager_1.0.0_amd64.deb
```

Remove with `sudo dpkg -r dotfile-manager` (or `apt remove dotfile-manager`
if installed via apt). Removal only ever deletes the files dpkg itself
installed — it never touches a user's `~/.config/dotfile-manager/` or
`~/dotfiles` store.

Files:

- `control.in` — package metadata template (`@VERSION@`/`@ARCH@`/
  `@INSTALLED_SIZE@` are substituted by `build-deb.sh`)
- `postinst` — refreshes the man page database (`mandb`) after install
- `prerm` — no-op; dotfile-manager never installs anything outside paths
  dpkg tracks
- `build-deb.sh` — the build script described above
