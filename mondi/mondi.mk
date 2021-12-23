CROSS_COMPILE ?= aarch64-linux-gnu-
MKDIR ?= mkdir -p
CP ?= cp
CAT ?= cat
TAR ?= tar
PWD = $(shell pwd)
SUDO ?= $(and $(filter pip,$(shell whoami)),sudo)
NATIVE_TRIPLE ?= amd64-linux-gnu
BUILD ?= $(PWD)/build
CROSS_CFLAGS = -Os --sysroot=$(BUILD)/pearl/install -B$(BUILD)/pearl/install -L$(BUILD)/pearl/install/lib -I$(BUILD)/pearl/install/include
CROSS_CC = $(BUILD)/pearl/toolchain/bin/aarch64-linux-gnu-gcc
CROSS_PATH = $(BUILD)/pearl/toolchain/bin
WITH_CROSS_PATH = PATH="$(CROSS_PATH):$$PATH"
WITH_CROSS_CFLAGS = CFLAGS="$(CROSS_CFLAGS)"
WITH_CROSS_COMPILE = CROSS_COMPILE=aarch64-linux-gnu-
WITH_CROSS_CC= CC="$(CROSS_CC)"
NATIVE_CODE_ENV = QEMU_LD_PREFIX=$(BUILD)/pearl/install LD_LIBRARY_PATH=$(BUILD)/pearl/install/lib
WITH_QEMU = $(NATIVE_CODE_ENV)

.SECONDEXPANSION:

all:

%/:
	$(MKDIR) $@

build/%: $(PWD)/build/%
	@true

%.xz: %
	xzcat -z --verbose < $< > $@

%.zstd: %
	zstd -cv < $< > $@

.PHONY: %}

$(BUILD)/debian/di-debootstrap.cpio: | $(BUILD)/debian/
	sudo rm -rf $(BUILD)/debian/di-debootstrap
	sudo DEBOOTSTRAP_DIR=$(PWD)/debian/debootstrap/debootstrap ./debian/debootstrap/debootstrap/debootstrap --foreign --arch=arm64 --include=build-essential,git,linux-image-cloud-arm64,bash,kmod,dash,wget,busybox,busybox-static,net-tools,libpam-systemd,file,xsltproc,mtools,openssl,mokutil,libx11-data,libx11-6,sharutils,dpkg-dev sid $(BUILD)/debian/di-debootstrap http://deb.debian.org/debian
	sudo chmod a+r -R $(BUILD)/debian/di-debootstrap/root
	sudo chmod a+x $(BUILD)/debian/di-debootstrap/root
	(cd $(BUILD)/debian/di-debootstrap/root; sudo git clone $(or $(DIREPO),https://github.com/pipcet/debian-installer))
	(cd $(BUILD)/debian/di-debootstrap/root/debian-installer; sudo mr checkout)
	sudo rm -f $(BUILD)/debian/di-debootstrap/init
	(echo '#!/bin/bash -x'; \
	echo "export PATH"; \
	echo "/debootstrap/debootstrap --second-stage"; \
	echo "/bin/busybox mount -t proc proc proc"; \
	echo "depmod -a"; \
	echo "modprobe virtio"; \
	echo "modprobe virtio_pci"; \
	echo "modprobe virtio_net"; \
	echo "modprobe virtio_blk"; \
	echo "modprobe virtio_scsi"; \
	echo "modprobe sd_mod"; \
	echo "mknod /dev/vda b 254 0"; \
	echo "dhclient -v eth0"; \
	echo "echo deb-src https://deb.debian.org/debian sid main >> /etc/apt/sources.list"; \
	echo "apt-get -y update"; \
	echo "apt-get -y dist-upgrade"; \
	echo "apt-get -y build-dep debian-installer anna"; \
	echo "apt-get -y clean"; \
	echo "(cd /root/debian-installer/packages/anna; ./debian/rules build)"; \
	echo "(cd /root/debian-installer/packages/anna; ./debian/rules binary)"; \
	echo "cp /root/debian-installer/packages/anna_*_arm64.udeb /root/debian-installer/installer/build/localudebs/"; \
	echo "rm -rf /root/debian-installer/packages"; \
	echo "(cd /root/debian-installer/installer/build; make build_netboot-gtk)"; \
	echo "uuencode 'netboot.tar.gz' < /root/debian-installer/installer/build/dest/netboot/gtk/netboot.tar.gz > /dev/vda"; \
	echo "sync"; \
	echo "poweroff -f") | sudo tee $(BUILD)/debian/di-debootstrap/init
	sudo chmod u+x $(BUILD)/debian/di-debootstrap/init
	(cd $(BUILD)/debian/di-debootstrap; sudo chown root.root .; sudo find . | sudo cpio -H newc -o) > $@

$(BUILD)/netboot.tar.gz: $(BUILD)/qemu-kernel $(BUILD)/debian/di-debootstrap.cpio | $(BUILD)/
	dd if=/dev/zero of=tmp bs=128M count=1
	qemu-system-aarch64 -drive if=virtio,index=0,media=disk,driver=raw,file=tmp -machine virt -cpu max -kernel $(BUILD)/qemu-kernel -m 7g -serial stdio -initrd ./build/debian/di-debootstrap.cpio -nic user,model=virtio -monitor none -smp 8 -nographic
	uudecode -o $@ < tmp
	rm -f tmp

$(BUILD)/netboot-initrd.cpio.gz: $(BUILD)/netboot.tar.gz | $(BUILD)/
	sudo rm -rf $(BUILD)/netboot-tmp
	sudo $(MKDIR) $(BUILD)/netboot-tmp
	sudo tar -C $(BUILD)/netboot-tmp -xzvf $<
	cp $(BUILD)/netboot-tmp/debian-installer/arm64/initrd.gz $@
	sudo rm -rf $(BUILD)/netboot-tmp
