Name:           xow
Version:        0.5
Release:        1%{?dist}
Summary:        xow Xbox One Wireless driver.

License:        GPLv2
URL:            https://medusalix.github.io/xow
Source0:        https://github.com/medusalix/xow/archive/v%{version}.tar.gz

BuildRequires:  libusb-devel fedora-packager fedora-review gcc-c++ cabextract
Requires:       systemd

%description

xow is a Linux user mode driver for the Xbox One wireless dongle.
It communicates with the dongle via `libusb` and provides joystick input through the `uinput` kernel module.
The input mapping is based on existing kernel drivers like [xpad](https://github.com/paroj/xpad).

The Xbox One wireless dongle requires a proprietary firmware to operate.
The firmware is included with the *Xbox - Net - 7/11/2017 12:00:00 AM - 1.0.46.1* driver available from *Microsoft Update Catalog*.
The package is automatically downloaded and extracted during the build process due to Microsoft's [Terms of Use](http://www.microsoft.com/en-us/legal/intellectualproperty/copyright/default.aspx), which strictly disallow the distribution of the firmware.
**By using xow, you accept Microsoft's license terms for their driver package.**

%global debug_package %{nil}

%prep
%autosetup


%build
%make_build BUILD=RELEASE BINDIR=%{_bindir} UDEVDIR=%{_udevrulesdir} MODLDIR=%{_modulesloaddir} MODPDIR=%{_modprobedir} SYSDDIR=%{_unitdir}

%install
rm -rf $RPM_BUILD_ROOT
%make_install BINDIR=%{_bindir} UDEVDIR=%{_udevrulesdir} MODLDIR=%{_modulesloaddir} MODPDIR=%{_modprobedir} SYSDDIR=%{_unitdir}

%files
%{_udevrulesdir}/99-xow.rules
%{_modulesloaddir}/xow-uinput.conf
%{_modprobedir}/xow-blacklist.conf
%{_unitdir}/xow.service
%{_bindir}/xow

%changelog
* Thu May 21 2020 Jairo Llopis
- 1st release
