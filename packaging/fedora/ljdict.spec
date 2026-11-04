Name:           ljdict
Version:        1.0
Release:        1%{?dist}
Summary:        Lightweight dictionary
License:        Unlicense
Source0:        %{name}-%{version}.tar.gz

BuildRequires:  cmake
BuildRequires:  ninja-build
BuildRequires:  gcc-c++
BuildRequires:  pkgconfig(gtkmm-4.0)
BuildRequires:  desktop-file-utils
BuildRequires:  libappstream-glib
Requires:       hicolor-icon-theme

%description
LjDict searches all enabled StarDict dictionaries as you type, ignoring case
and diacritics, and shows the articles side by side.

%prep
%autosetup

%build
%cmake -G Ninja
%cmake_build

%install
%cmake_install

%check
desktop-file-validate %{buildroot}%{_datadir}/applications/net.jegor.ljdict.gtk.desktop
appstream-util validate-relax --nonet %{buildroot}%{_metainfodir}/net.jegor.ljdict.gtk.metainfo.xml
%ctest

%files
%license LICENSE
%doc README.md
%{_bindir}/ljdict
%{_datadir}/applications/net.jegor.ljdict.gtk.desktop
%{_metainfodir}/net.jegor.ljdict.gtk.metainfo.xml
%{_datadir}/icons/hicolor/*/apps/net.jegor.ljdict.gtk.png

%changelog
* Wed Nov 04 2026 Yegor Gluhov <git@jegor.net> - 1.0-1
- First package
