$(BUILD)/debian/debootstrap/stage1.tar: | $(BUILD)/debian/debootstrap/
	sudo DEBOOTSTRAP_DIR=$(PWD)/debian/debootstrap/debootstrap ./debian/debootstrap/debootstrap/debootstrap --foreign --arch=arm64 --include=dash,wget,busybox,busybox-static,network-manager,openssh-client,net-tools,libpam-systemd,cryptsetup,lvm2,memtool,nvme-cli,watchdog,minicom,device-tree-compiler,file,gpm,ssh,usbutils,pciutils sid $(BUILD)/debian/debootstrap/stage1 http://deb.debian.org/debian
	(cd $(BUILD)/debian/debootstrap/stage1; sudo tar c .) > $@

$(BUILD)/debian/debootstrap/stage15.tar: $(BUILD)/debian/debootstrap/stage1.tar
	$(MKDIR) $(BUILD)/debian/debootstrap/stage15
	(cd $(BUILD)/debian/debootstrap/stage15; sudo tar x) < $<
	(cd $(BUILD)/debian/debootstrap/stage15/var/cache/apt/archives/; for a in *.deb; do sudo dpkg-deb -R $$a $$a.d; sudo dpkg-deb -b -Znone $$a.d; sudo mv $$a.d.deb $$a; sudo rm -rf $$a.d; done)
#	for a in $(BUILD)/debian/debootstrap/stage15/var/cache/apt/archives/*.deb; do sudo dpkg -x $$a $(BUILD)/debian/debootstrap/stage15; done
	(cd $(BUILD)/debian/debootstrap/stage15; sudo tar c .) > $@

$(BUILD)/debian/full-installer.cpio.gz: | $(BUILD)/debian/
	wget -O $@ https://github.com/pipcet/debian-installer/releases/latest/download/netboot-initrd.cpio.gz

$(BUILD)/debian/full-installer.cpio: $(BUILD)/debian/full-installer.cpio.gz
	gunzip < $< > $@

$(BUILD)/debian/installer.cpio: $(BUILD)/debian/full-installer.cpio
	sudo rm -rf $(BUILD)/debian/full-installer.d
	sudo $(MKDIR) $(BUILD)/debian/full-installer.d
	(cd $(BUILD)/debian/full-installer.d; sudo cpio -id) < $<
	sudo rm -rf $(BUILD)/debian/full-installer.d/boot
	sudo rm -rf $(BUILD)/debian/full-installer.d/lib/modules
	(cd $(BUILD)/debian/full-installer.d; sudo find | sudo cpio -o) > $@

$(BUILD)/debian/installer: debian/injected/bin/installer
	$(CP) $< $@
	chmod u+x $@

$(BUILD)/debian.cpio: $(BUILD)/debian/debootstrap/stage15.tar $(BUILD)/debian/installer.cpio $(BUILD)/debian/installer
	$(MKDIR) $(BUILD)/debian/cpio.d
	(cd $(BUILD)/debian/cpio.d; sudo tar x) < $<
	sudo cp $(BUILD)/debian/installer.cpio $(BUILD)/debian/cpio.d
	sudo cp $(BUILD)/debian/installer $(BUILD)/debian/cpio.d/bin
	sudo chown root.root $(BUILD)/debian/cpio.d
	sudo ln -sf sbin/init $(BUILD)/debian/cpio.d/init
	(cd $(BUILD)/debian/cpio.d; sudo find | sudo cpio -o -H newc) > $@

$(BUILD)/debian.cpio.gz: $(BUILD)/debian.cpio
	gzip < $< > $@

$(BUILD)/debian.cpio.zstd: $(BUILD)/debian.cpio
	zstd -22 --ultra --long --verbose < $< > $@

$(BUILD)/debian.cpio.xz: $(BUILD)/debian.cpio
	xz --compress < $< > $@
