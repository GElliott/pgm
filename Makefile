# figure out what kind of host we are running on
host-arch := $(shell uname -m | \
	sed -e s/i.86/i386/ -e s/sun4u/sparc64/ -e s/arm.*/arm/)

# ##############################################################################
# User variables

# user variables can be specified in the environment or in a .config file
-include .config

# ARCH -- what architecture are we compiling for?
ARCH ?= ${host-arch}

# LIBLITMUS -- where to find liblitmus?
LIBLITMUS ?= ../liblitmus

# Where to find libpgm?
LIBPGM = .

# ##############################################################################
# Internal configuration.

# compiler flags
flags-std      = -std=gnu++11
flags-optim    = -O2 -march=native
flags-debug    = -Wall -Werror -Wno-unused-function -Wno-sign-compare
flags-api      = -D_XOPEN_SOURCE=600 -D_GNU_SOURCE -pthread

# Comment out 'flags-litmus' to disable Litmus support
#flags-litmus   = -D_USE_LITMUS

# architecture-specific flags
flags-i386     = -m32
flags-x86_64   = -m64
flags-sparc64  = -mcpu=v9 -m64
# default: none

# name of the directory that has the arch headers in the Linux source
include-i386     = x86
include-x86_64   = x86
include-sparc64  = sparc
# default: the arch name
include-${ARCH} ?= ${ARCH}

# name of the file(s) that holds the actual system call numbers
unistd-i386      = unistd.h unistd_32.h
unistd-x86_64    = unistd.h unistd_64.h
# default: unistd.h
unistd-${ARCH}  ?= unistd.h

# where to find header files
ifdef flags-litmus
liblitmus-headers = -I${LIBLITMUS}/include -I${LIBLITMUS}/arch/${include-${ARCH}}/include
else
liblitmus-headers =
endif
headers = -I${LIBPGM}/include

# combine options
CPPFLAGS = ${flags-std} ${flags-optim} ${flags-debug} ${flags-api} ${flags-${ARCH}} -DARCH=${ARCH} ${liblitmus-headers} ${headers}
LDFLAGS  = ${flags-${ARCH}}

# how to link against liblitmus
ifdef flags-litmus
liblitmus-flags = -L${LIBLITMUS} -llitmus
else
liblitmus-flags =
endif

# how to link against libpgm
libpgm-flags = -L${LIBPGM} -lpgm

CPP = g++

# incorporate cross-compiler (if any)
CPP  := ${CROSS_COMPILE}${CPP}
LD  := ${CROSS_COMPILE}${LD}
AR  := ${CROSS_COMPILE}${AR}

# ##############################################################################
# Targets

all     = lib ${tools}
tools   = cvtest ringtest basictest datapassingtest sockstreamtest pingpong depthtest pgmrt

.PHONY: all lib clean dump-config TAGS tags cscope help

all: ${all}

clean:
	rm -f ${tools}
	rm -f *.o *.d *.d.* *.a
	rm -f tags TAGS cscope.files cscope.out

# Emacs Tags
TAGS:
	@echo TAGS
	@find . -type f -and  -iname '*.[ch]' | xargs etags

# Vim Tags
tags:
	@echo tags
	@find . -type f -and  -iname '*.[ch]' | xargs ctags

# cscope DB
cscope:
	@echo cscope
	@find . -type f -and  -iname '*.[ch]' | xargs printf "%s\n" > cscope.files
	@cscope -b

# ##############################################################################
# libpgm

lib: libpgm.a

# all .c file in src/ are linked into liblitmus
vpath %.cpp src/
obj-lib = $(patsubst src/%.cpp,%.o,$(wildcard src/*.cpp))

libpgm.a: ${obj-lib}
	${AR} rcs $@ $+


# ##############################################################################
# Tools that link with libpgm

# these source files are found in bin/
vpath %.cpp tools

obj-cvtest = cvtest.o
lib-cvtest = -lpthread -lm -lrt -lboost_graph -lboost_filesystem -lboost_system ${liblitmus-flags}

obj-ringtest = ringtest.o
lib-ringtest = -lpthread -lm -lrt -lboost_graph -lboost_filesystem -lboost_system ${liblitmus-flags}

obj-basictest = basictest.o
lib-basictest = -lpthread -lm -lrt -lboost_graph -lboost_filesystem -lboost_system ${liblitmus-flags}

obj-datapassingtest = datapassingtest.o
lib-datapassingtest = -lpthread -lm -lrt -lboost_graph -lboost_filesystem -lboost_system ${liblitmus-flags}

obj-sockstreamtest = sockstreamtest.o
lib-sockstreamtest = -lpthread -lm -lrt -lboost_graph -lboost_filesystem -lboost_system ${liblitmus-flags}

obj-pgmrt = pgmrt.o
lib-pgmrt = -lpthread -lm -lrt -lboost_graph -lboost_filesystem -lboost_system -lboost_program_options ${liblitmus-flags}

obj-pingpong = pingpong.o
lib-pingpong = -lpthread -lm -lrt -lboost_graph -lboost_system -lboost_thread ${liblitmus-flags}

obj-depthtest = depthtest.o
lib-depthtest = -lpthread -lm -lrt -lboost_graph -lboost_filesystem -lboost_system ${liblitmus-flags}

# ##############################################################################
# Build everything that depends on liblitmus.

.SECONDEXPANSION:
${tools}: $${obj-$$@} libpgm.a
	$(CPP) -o $@ $(LDFLAGS) ${ldf-$@} $(filter-out libpgm.a,$+) $(LOADLIBS) $(LDLIBS) ${libpgm-flags} ${lib-$@}

# ##############################################################################
# Dependency resolution.

vpath %.cpp src/ tools/

obj-all = ${sort ${foreach target,${all},${obj-${target}}}}

# rule to generate dependency files
%.d: %.cpp
	@set -e; rm -f $@; \
		$(CPP) -MM $(CPPFLAGS) $< > $@.$$$$; \
		sed 's,\($*\)\.o[ :]*,\1.o $@ : ,g' < $@.$$$$ > $@; \
		rm -f $@.$$$$

ifeq ($(MAKECMDGOALS),)
MAKECMDGOALS += all
endif

ifneq ($(filter-out clean help,$(MAKECMDGOALS)),)

# Pull in dependencies.
-include ${obj-all:.o=.d}

# Let's make sure the liblitmus header path is ok.
config-ok  := $(shell test -d "${LIBLITMUS}" || echo invalid path. )
ifneq ($(strip $(config-ok)),)
$(info (!!) Could not find liblitmus at ${LIBLITMUS}: ${config-ok})
$(info (!!) Are you sure the path is correct?)
$(error Cannot build without liblitmus)
endif

endif
