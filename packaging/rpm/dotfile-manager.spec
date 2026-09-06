Name:           dotfile-manager
Version:        1.0.0
Release:        1%{?dist}
Summary:        Safe CLI utility for discovering, comparing, and backing up dotfiles

License:        MIT
URL:            https://github.com/reecelabson/dotfile-manager
Source0:        %{name}-%{version}.tar.gz

BuildRequires:  cmake >= 3.16
BuildRequires:  gcc-c++
BuildRequires:  json-devel

%description
dotfile-manager discovers, inventories, compares, backs up, and restores
configuration files ("dotfiles") between a user's home directory and a
managed store directory. It is designed around strict safety defaults:
bounded, read-only discovery (never scans the whole home directory or
sensitive credential paths); no destructive operation without an explicit
--force after a --dry-run preview; no implicit conversion of files into
symlinks; and no recursive deletes.

%prep
%setup -q

%build
cmake -S . -B redhat-linux-build \
    -DCMAKE_BUILD_TYPE=Release \
    -DDFM_BUILD_TESTS=OFF \
    -DCMAKE_INSTALL_PREFIX=%{_prefix} \
    -DCMAKE_INSTALL_LIBDIR=%{_lib}
cmake --build redhat-linux-build %{?_smp_mflags}

%install
DESTDIR=%{buildroot} cmake --install redhat-linux-build

%files
%{_bindir}/dotfile-manager
%{_mandir}/man1/dotfile-manager.1.gz
%{_datadir}/doc/%{name}/README.md
%{_datadir}/doc/%{name}/LICENSE

%changelog
* Sat Sep 05 2026 Reece Labson <reecelabson@gmail.com> - 1.0.0-1
- Initial release: discover, list, add, remove, status, diff, backup,
  apply, restore, check, report, version commands with strict
  no-silent-overwrite / bounded-discovery / no-implicit-symlink safety
  guarantees.
