Name: whiteboard-sib-access
Summary: smart-m3 component
Version: 1.0
Release: 1
Group: Applications/System
License: GPLv3
URL: https://github.com/smart-m3/
Source0: whiteboard-sib-access-1.0.tar.bz2
BuildRoot: %{_tmppath}/whiteboard-sib-access-root
BuildRequires: libwhiteboard >= 1.0
Requires: libwhiteboard >= 1.0 whiteboard >= 1.0
%description


%prep
%setup -q -n %{name}-%{version}

%build
export PKG_CONFIG_PATH=/usr/local/lib/pkgconfig/
./autogen.sh
make %{?_smp_mflags}

%install
rm -rf $RPM_BUILD_ROOT
make DESTDIR=$RPM_BUILD_ROOT install

%clean
make clean
rm -rf $RPM_BUILD_ROOT


%files
%defattr(-, root, root)
/usr/

%post
echo /usr/local/lib >> /etc/ld.so.conf
ldconfig

%changelog
* Fri Oct 20 2013 Gubin Pavel
- Initial build


