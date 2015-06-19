Summary: Transmits / multicasts files over unreliable UDP
Name: feclib
Version: 0.90
Release: 1
Copyright: Open Source + Advert
Group: Networking/File transfer
Packager: Nic Roets<feclib@sig.co.za>
Vendor: Nic Roets<feclib@sig.co.za>
URL: http://feclib.sourceforge.net

%description
Transmits / multicasts files over unreliable UDP.

%prep
tar xzf $RPM_SOURCE_DIR/$RPM_PACKAGE_NAME-$RPM_PACKAGE_VERSION.tgz
%build
cd $RPM_PACKAGE_NAME-$RPM_PACKAGE_VERSION
make
%install
cd $RPM_PACKAGE_NAME-$RPM_PACKAGE_VERSION
rm -rf $RPM_BUILD_ROOT
mkdir -p $RPM_BUILD_ROOT/usr/bin
install -s -m 755 fecrecv $RPM_BUILD_ROOT/usr/bin/fecrecv
install -s -m 755 fecsend $RPM_BUILD_ROOT/usr/bin/fecsend

$clean
rm -rf $RPM_BUILD_ROOT

%files
%defattr(-,root,root)
%doc $RPM_PACKAGE_NAME-$RPM_PACKAGE_VERSION/documentation.html

/usr/bin/fecrecv
/usr/bin/fecsend
