CFLAGS+=-Wall -W
CFLAGS += -D_GNU_SOURCE
OBJS=$(subst .c,.o,$(wildcard *.c))
BIN=main-menu
LIBS=-ldebconfclient -ldebian-installer

ifdef DEBUG
CFLAGS:=$(CFLAGS) -DDODEBUG=1
else
CFLAGS:=$(CFLAGS) -Os -fomit-frame-pointer
endif

all: $(BIN)

$(BIN): $(OBJS)
	$(CC) $(LDFLAGS) -o $(BIN) $(OBJS) $(LIBS)

demo: $(BIN)
	ln -sf debian/templates main-menu.templates
	/usr/share/debconf/frontend ./$(BIN)
	rm -f main-menu.template

# Size optimized binary target.
small: CFLAGS:=-Os $(CFLAGS) -DSMALL
small: clean $(BIN)
	ls -l $(BIN)

clean:
	-rm -f $(BIN) $(OBJS) *~
