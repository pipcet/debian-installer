dir = lib/debian-installer-startup.d

DEB_HOST_ARCH_CPU   := $(shell dpkg-architecture -qDEB_HOST_ARCH_CPU 2>/dev/null)
DEB_HOST_ARCH_OS    := $(shell dpkg-architecture -qDEB_HOST_ARCH_OS 2>/dev/null)

# Take account of old dpkg-architecture output.
ifeq ($(DEB_HOST_ARCH_CPU),)
  DEB_HOST_ARCH_CPU   := $(shell dpkg-architecture -qDEB_HOST_GNU_CPU)
  ifeq ($(DEB_HOST_ARCH_CPU),x86_64)
    DEB_HOST_ARCH_CPU   := amd64
  endif
endif
ifeq ($(DEB_HOST_ARCH_OS),)
  DEB_HOST_ARCH_OS    := $(shell dpkg-architecture -qDEB_HOST_GNU_SYSTEM)
endif

files = \
	S10syslog \
	S20templates \
	S35term \
	S50entropy-source

ifeq ($(DEB_HOST_ARCH_OS),linux)
  ifneq (,$(filter i386 amd64,$(DEB_HOST_ARCH_CPU)))
    files += \
	S40framebuffer-module-linux-x86
   endif
  
  ifeq ($(DEB_HOST_ARCH_CPU),powerpc)
    files += \
	S05fancontrol-linux-powerpc \
	S45keyboard-linux-powerpc
  endif

  ifneq (,$(filter alpha,$(DEB_HOST_ARCH_CPU)))
    files += \
	S40framebuffer-module-linux-alpha \
	S04countcpus-linux-alpha
  endif

  ifneq (,$(filter hppa,$(DEB_HOST_ARCH_CPU)))
    files += \
	S04countcpus-linux-hppa
  endif

  ifneq (,$(filter arm armel armhf,$(DEB_HOST_ARCH_CPU)))
    files += \
	S40framebuffer-module-linux-armel \
	S99signal-status
  endif

  files += \
	S01mount \
	S02module-params \
	S37speakup
endif

include ../../../Makefile.inc
