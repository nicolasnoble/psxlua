# Builds luac as a ps-exe, so the compiler runs on the console (or in an
# emulator) instead of on the host. Input and output go through PCDRV, and
# argv is read from unmapped memory at 0x40000000 - see args.lua.
#
# psxlua does not depend on nugget: nugget's psyqo-lua consumes psxlua, so
# vendoring it here would close a cycle. Point NUGGET at a checkout instead.
#
#   make psx-luac NUGGET=/path/to/nugget
#
# liblua.a must be built for the same target first; the psx-luac rule in
# Makefile does that for you.

ifeq ($(strip $(NUGGET)),)
$(error NUGGET is not set - point it at a nugget checkout, e.g. make psx-luac NUGGET=../../nugget)
endif

TARGET = luac
TYPE = ps-exe

SRCS = \
luac.c \
psx-heap.c \
psx-glue.s \
$(NUGGET)/common/syscalls/printf.s \
$(NUGGET)/common/crt0/memory-s.s \
$(NUGGET)/common/crt0/memory-c.c \

LIBRARIES = liblua.a

CPPFLAGS += -I. -DLUA_COMPAT_ALL

include $(NUGGET)/common.mk

liblua.a:
	$(MAKE) -f Makefile a TARGET=psx
