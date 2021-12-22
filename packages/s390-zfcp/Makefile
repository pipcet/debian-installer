#
# Makefile to build the s390 FCP device configuration panels for the
# Debian Installer
#

CC := gcc
CFLAGS := -Wall -W -pipe -Os -I/usr/include/sysfs -D_GNU_SOURCE
LDFLAGS := -ldebconfclient -ldebian-installer

DEB_BUILD_OPTIONS=debug

ifneq (,$(findstring debug,$(DEB_BUILD_OPTIONS)))
CFLAGS += -g -ggdb
STRIP = /bin/true
else
CFLAGS += -fomit-frame-pointer
STRIPTOOL=strip
STRIP = $(STRIPTOOL) --remove-section=.note --remove-section=.comment
endif

S390_DEV_LIB = s390-dev.o s390-dev-misc.o s390-dev-path.o
ZFCP_CONFIG = zfcp-config

all: $(ZFCP_CONFIG)

$(S390_DEV_LIB): s390-dev.h

zfcp-config: zfcp-config.o $(S390_DEV_LIB) s390-zfcp.o
	$(CC) -o $@ $^ $(LDFLAGS)
	$(STRIP) $@

clean:
	rm -f $(ZFCP_CONFIG) *.o

.PHONY: clean
