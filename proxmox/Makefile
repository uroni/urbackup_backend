# Installs the UrBackup backup provider for Proxmox VE, or builds a Debian package of it:
#   make install
#   make deb

PACKAGE = urbackup-pve
VERSION = 0.1.0

DESTDIR =
PERLDIR = /usr/share/perl5
SCRIPTDIR = /usr/share/urbackup-pve

BUILDDIR = build/$(PACKAGE)_$(VERSION)_all
DEB = build/$(PACKAGE)_$(VERSION)_all.deb

.PHONY: all install deb clean

all:

install:
	install -D -m 0644 src/PVE/Storage/Custom/UrBackupPlugin.pm \
		$(DESTDIR)$(PERLDIR)/PVE/Storage/Custom/UrBackupPlugin.pm
	install -D -m 0644 src/PVE/BackupProvider/Plugin/UrBackup.pm \
		$(DESTDIR)$(PERLDIR)/PVE/BackupProvider/Plugin/UrBackup.pm
	install -D -m 0755 scripts/create_volume_snapshot $(DESTDIR)$(SCRIPTDIR)/create_volume_snapshot
	install -D -m 0755 scripts/remove_volume_snapshot $(DESTDIR)$(SCRIPTDIR)/remove_volume_snapshot
	install -D -m 0644 conf/modules-load.conf $(DESTDIR)/etc/modules-load.d/urbackup-pve.conf
	install -D -m 0644 conf/modprobe.conf $(DESTDIR)/etc/modprobe.d/urbackup-pve.conf

deb: clean
	$(MAKE) install DESTDIR=$(BUILDDIR)
	install -D -m 0644 README.md $(BUILDDIR)/usr/share/doc/$(PACKAGE)/README.md
	install -d $(BUILDDIR)/DEBIAN
	sed 's/@VERSION@/$(VERSION)/' debian/control.in > $(BUILDDIR)/DEBIAN/control
	install -m 0644 debian/conffiles $(BUILDDIR)/DEBIAN/conffiles
	install -m 0755 debian/postinst debian/postrm $(BUILDDIR)/DEBIAN/
	dpkg-deb --root-owner-group --build $(BUILDDIR) $(DEB)

clean:
	rm -rf build
