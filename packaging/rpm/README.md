# RPM packaging

Build an RPM (and source RPM) for dotfile-manager:

```sh
cd packaging/rpm
./build-rpm.sh
```

This script:

1. Creates a clean source tarball (`~/rpmbuild/SOURCES/dotfile-manager-1.0.0.tar.gz`)
   containing only `CMakeLists.txt`, `LICENSE`, `README.md`, `src/`,
   `include/`, `man/`, and `cmake/` — no build artifacts, no test suite,
   no VCS metadata.
2. Runs `rpmbuild -ba dotfile-manager.spec`, producing:
   - `~/rpmbuild/RPMS/<arch>/dotfile-manager-1.0.0-1.<arch>.rpm`
   - `~/rpmbuild/RPMS/<arch>/dotfile-manager-debuginfo-1.0.0-1.<arch>.rpm`
   - `~/rpmbuild/SRPMS/dotfile-manager-1.0.0-1.src.rpm`

Install with:

```sh
sudo rpm -i ~/rpmbuild/RPMS/x86_64/dotfile-manager-1.0.0-1.*.rpm
```

or via `dnf install ~/rpmbuild/RPMS/x86_64/dotfile-manager-1.0.0-1.*.rpm`
on Fedora/RHEL so runtime dependencies are resolved automatically.

Removal (`sudo rpm -e dotfile-manager`) only deletes the files RPM itself
tracks — it never touches a user's `~/.config/dotfile-manager/` or
`~/dotfiles` store.

## Notes on build portability

`dotfile-manager.spec` builds with plain `cmake -S/-B` and
`cmake --build`/`cmake --install` invocations rather than Fedora's
`%cmake`/`%cmake_build`/`%cmake_install` macros, so it builds correctly
whether or not `redhat-rpm-config` is present. On Fedora/RHEL/openSUSE
this is functionally equivalent; `build-rpm.sh` also auto-detects
non-RPM-native build hosts (no `dnf`/`yum`/`zypper`) and passes
`--nodeps` in that case only, since such a host's package database
cannot resolve the spec's RPM-named `BuildRequires` even when equivalent
packages are installed under their distribution's own names.

## Requirements

`BuildRequires` in the spec: `cmake >= 3.16`, `gcc-c++`, `json-devel`
(the `nlohmann-json` development headers; package name varies — `json-devel`
on Fedora/RHEL, `nlohmann-json-devel` on some others).
