Name:       harbour-pangolin-ui
Summary:    Start / stop GUI for the Pangolin CLI
Version:    0.4.0
Release:    1
# App and helper: MIT. Bundled /usr/bin/pangolin (fosrl/cli + olm patch): AGPL-3.0.
License:    MIT and AGPL-3.0-only
URL:        https://github.com/fosrl/cli
Source0:    %{name}-%{version}.tar.bz2
# Only aarch64 devices are supported.
ExclusiveArch: aarch64

# The bundled CLI is a static Go binary without a GNU build-id.
%global _missing_build_ids_terminate_build 0
# fosrl/cli at this tag, built with pangolin-cli/olm-systemd238.patch and
# bundled as /usr/bin/pangolin. Build it into rpm/ before packaging:
# sh pangolin-cli/build.sh (mb2 uses rpm/ as %%{_sourcedir}).
%define pangolin_version 0.19.0
Source1:    pangolin-cli_linux_arm64

# Silica UI
Requires:   sailfishsilica-qt5 >= 0.10.9
# Top Menu toggle (QML loaded by lipstick): Nemo.DBus 2.0,
# com.jolla.settings (jolla-settings) and org.nemomobile.lipstick are part of
# every Sailfish OS image; only the D-Bus QML plugin is named explicitly.
Requires:   nemo-qml-plugin-dbus-qt5
Requires:   systemd

BuildRequires:  pkgconfig(sailfishapp) >= 1.0.2
BuildRequires:  pkgconfig(Qt5Core)
BuildRequires:  pkgconfig(Qt5Qml)
BuildRequires:  pkgconfig(Qt5Quick)
BuildRequires:  desktop-file-utils

%description
A Sailfish Silica front-end for the Pangolin CLI (fosrl/cli): start and
stop the client tunnel (pangolin up / pangolin down), show its status
(pangolin status --json), log in, and view command output and the client
log. Includes a Top Menu quick toggle that starts/stops the systemd user
unit harbour-pangolin.service.

Bundles the Pangolin CLI %{pangolin_version} as /usr/bin/pangolin, built with
a small olm patch for systemd-resolved 238 (pangolin-cli/olm-systemd238.patch),
and a small setuid-root helper that can only run `pangolin up --attach`, so no
sudo or devel-su setup is needed.

Not Jolla Harbour (store) compliant: setuid helper, jolla-settings entries
and a systemd user unit. Intended for OpenRepos / Chum / sideloading.

%prep
%autosetup -n %{name}-%{version}

%build
%qmake5
%make_build
# mb2/Qt Creator shadow-build outside the source tree (%%{_sourcedir} is the
# project's rpm/ dir); tarball builds (OBS/Chum) have the sources in cwd.
helper_src=helper/pangolin-helper.c
[ -f "$helper_src" ] || helper_src=%{_sourcedir}/../helper/pangolin-helper.c
%{__cc} %{optflags} -std=gnu99 -o pangolin-helper "$helper_src"

%install
%qmake5_install

install -D -m755 %{SOURCE1} %{buildroot}%{_bindir}/pangolin
install -D -m755 pangolin-helper %{buildroot}%{_libexecdir}/%{name}/pangolin-helper

desktop-file-install --delete-original \
  --dir %{buildroot}%{_datadir}/applications \
   %{buildroot}%{_datadir}/applications/*.desktop

%files
%defattr(-,root,root,-)
%{_bindir}/%{name}
%{_bindir}/pangolin
%dir %{_libexecdir}/%{name}
%attr(4755,root,root) %{_libexecdir}/%{name}/pangolin-helper
%{_datadir}/%{name}
%{_datadir}/applications/%{name}.desktop
%{_datadir}/icons/hicolor/*/apps/%{name}.png
%{_datadir}/jolla-settings/entries/%{name}.json
%{_datadir}/jolla-settings/pages/%{name}
/usr/lib/systemd/user/harbour-pangolin.service

# The unit belongs to each user's systemd --user instance; a root scriptlet
# cannot reload it. A new unit file is normally picked up on first use; the
# app also runs `systemctl --user daemon-reload` before its first start.
