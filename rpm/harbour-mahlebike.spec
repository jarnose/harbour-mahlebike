Name:       harbour-mahlebike

Summary:    Mahle Bike
Version:    1.1.0
Release:    1
License:    MIT
URL:        https://github.com/jarnose/harbour-mahlebike
Source0:    %{name}-%{version}.tar.bz2
Requires:   sailfishsilica-qt5 >= 0.10.9
BuildRequires:  pkgconfig(sailfishapp) >= 1.0.2
BuildRequires:  pkgconfig(Qt5Core)
BuildRequires:  pkgconfig(Qt5Qml)
BuildRequires:  pkgconfig(Qt5Quick)
BuildRequires:  pkgconfig(Qt5Sql)
BuildRequires:  pkgconfig(Qt5DBus)
BuildRequires:  pkgconfig(Qt5Positioning)
# No pkgconfig(Qt5Bluetooth): Sailfish OS's Qt5 has no QtConnectivity. The bike is reached through
# BlueZ's D-Bus API with Qt5DBus instead.
# No pkgconfig(Qt5LinguistTools) virtual exists; CMake finds lrelease through the CMake package
# config that this plain package provides.
BuildRequires:  qt5-qttools-linguist
BuildRequires:  desktop-file-utils
BuildRequires:  cmake

%description
Shows what an e-bike with a Mahle drive system (ebikemotion X20, X25, X35+, iWoc and HUS bikes sold
as Orbea, Scott, Bianchi, Pinarello, Schindelhauer and others) reports over Bluetooth LE: speed,
assist level, battery charge, motor and rider power, temperatures, trip, odometer, range and fault
codes. It can record rides, with the phone's GPS track if you want one, and export them as GPX or CSV.

Everything stays on the phone. The app has no network permission and needs no account.

The bike talks to one phone at a time: close the official My SmartBike app before connecting.

%prep
%setup -q -n %{name}-%{version}

%build

%cmake
%make_build

%install
%make_install

desktop-file-install --delete-original         --dir %{buildroot}%{_datadir}/applications                %{buildroot}%{_datadir}/applications/*.desktop

%files
%defattr(-,root,root,-)
%{_bindir}/%{name}
%{_datadir}/%{name}
%{_datadir}/applications/%{name}.desktop
%{_datadir}/icons/hicolor/*/apps/%{name}.png
