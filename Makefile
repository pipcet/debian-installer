all:

build/%: $(PWD)/build/%
	@true

include g/github/github.mk
include mondi/mondi.mk
