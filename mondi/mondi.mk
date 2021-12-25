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

-include mondi/deb.mk
-include mondi/debootstrap.mk

$(BUILD)/debian/root1.cpio.gz: | $(BUILD)/debian/
	wget -O $@ https://github.com/pipcet/debian-rootfs/releases/latest/download/root1.cpio.gz

$(BUILD)/debian/script.bash: | $(BUILD)/debian/
	(echo "#!/bin/bash -e"; \
	echo "apt-get install ca-certificates"; \
	echo "cd /root; git clone https://github.com/pipcet/debian-installer -b mondi"; \
	echo "cd /root/debian-installer/packages/anna; ./debian/rules build"; \
	echo "cd /root/debian-installer/packages/anna; ./debian/rules binary"; \
	echo "cp /root/debian-installer/packages/anna__*_arm64.udeb /root/debian-installer/installer/build/localudebs/"; \
	echo "cd /root/debian-installer/packages/busybox; ./debian/rules build"; \
	echo "cd /root/debian-installer/packages/busybox; ./debian/rules binary"; \
	echo "cp /root/debian-installer/packages/busybox-udeb*.udeb /root/debian-installer/installer/build/localudebs/"; \
	echo "rm -rf /root/debian-installer/packages"; \
	echo "cd /root/debian-installer/installer/build; make build_netboot-gtk"; \
	echo "uuencode 'netboot.tar.gz' < /root/debian-installer/installer/build/dest/netboot/gtk/netboot.tar.gz > /dev/vda") > $@

$(BUILD)/netboot.tar.gz: $(BUILD)/debian/script.bash $(BUILD)/qemu-kernel $(BUILD)/debian/root1.cpio.gz | $(BUILD)/
	dd if=/dev/zero of=tmp bs=128M count=1
	uuencode /dev/stdout < $< | dd conv=notrunc of=tmp
	qemu-system-aarch64 -drive if=virtio,index=0,media=disk,driver=raw,file=tmp -machine virt -cpu max -kernel $(BUILD)/qemu-kernel -m 7g -serial stdio -initrd $(BUILD)/debian/root1.cpio.gz -nic user,model=virtio -monitor none -smp 8 -nographic
	uudecode -o $@ < tmp
	rm -f tmp

$(BUILD)/netboot-initrd.cpio.gz: $(BUILD)/netboot.tar.gz | $(BUILD)/
	sudo rm -rf $(BUILD)/netboot-tmp
	sudo $(MKDIR) $(BUILD)/netboot-tmp
	sudo tar -C $(BUILD)/netboot-tmp -xzvf $<
	cp $(BUILD)/netboot-tmp/debian-installer/arm64/initrd.gz $@
	sudo rm -rf $(BUILD)/netboot-tmp
