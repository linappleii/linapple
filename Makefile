#! /usr/bin/make -f

# TODO conform to
# https://www.gnu.org/prep/standards/html_node/Standard-Targets.html

PACKAGE     := linapple
VERSION     := 2.3.0

#Compiler and Linker
CC          := g++

# Where does this get installed
PREFIX      ?= /usr/local
DESTDIR     ?= $(PREFIX)

#Application configuration location
CONFIGDIR   ?= etc/$(PACKAGE)

#Where assets (font file, etc) are read from
# (You can build a version that loads assets from the source tree with: DATADIR=`pwd`/res make )
DATADIR     ?= share/$(PACKAGE)

PKGDIR="pkg/$(PACKAGE)"

#The Target Binary Program
TARGET      := linapple

#The Directories, Source, Includes, Objects, Binary and Resources
SRCDIR      := src
INCDIR      := inc
BINDIR       = bin
OBJDIR       = obj
RESDIR      := res
SRCEXT      := cpp
DEPEXT      := d
OBJEXT      := o
IMGEXT      := png
XPMEXT      := xpm
SYMEXT      := SYM

BUILDDIR    := build/$(OBJDIR)
TARGETDIR   := build/$(BINDIR)

#Flags, Libraries and Includes

SDL_CONFIG ?= sdl-config
SDL_CFLAGS = $(shell $(SDL_CONFIG) --cflags)
SDL_LIBS = $(shell $(SDL_CONFIG) --libs)
SDL_LIBS +=  $(shell pkg-config SDL_image --libs)

CURL_CONFIG ?= curl-config
CURL_CFLAGS = $(shell $(CURL_CONFIG) --cflags)
CURL_LIBS = $(shell $(CURL_CONFIG) --libs)

#By default, optimize the executable.
CFLAGS := -Wall -O3 -ansi -c -std=c++11

ifdef PROFILING
CFLAGS := -Wall -O0 -pg -ggdb -ansi -c
LFLAGS := -pg
endif

ifdef DEBUG
CFLAGS := -O0 -ggdb -ansi -c -finstrument-functions -std=c++11 -Wno-write-strings -Werror
endif

CFLAGS += -DASSET_DIR=\"$(DATADIR)\" -DVERSIONSTRING=\"$(VERSION)\"
CFLAGS += $(SDL_CFLAGS)
CFLAGS += $(CURL_CFLAGS)
# Do not complain about XPMs
CFLAGS += -Wno-write-strings

LIB    := $(SDL_LIBS) $(CURL_LIBS) -lz -lzip -pthread -lX11
INC    := -I$(INCDIR) -I/usr/local/include
INCDEP := -I$(INCDIR)

# Check if user has compiled the source
define COMPILED

 Sorry, but you must first compile the program's source code. Read INSTALL.md
 file to learn how to do this.

endef
export COMPILED

# Calculate the total size of installed files
#   /usr/local/bin/linapple
#   /usr/local/etc/linapple.conf
#   /usr/local/share/Master.dsk
size1 := $(shell du -s ./build/bin 2>&1 | cut -f 1)
size2 := $(shell du -s ./build/etc/ 2>&1 | cut -f 1)
size3 := $(shell du -s ./build/share/ 2>&1 | cut -f 1)
SIZE  := $(shell echo `expr $(size1) + $(size2) + $(size3) 2>&1` )

# DEBIAN/conffiles
define CONFFILES
$(DESTDIR)/$(CONFIGDIR)/linapple.conf
$(DESTDIR)/$(DATADIR)/Master.dsk
endef
export CONFFILES

# Architecture
ARCH := all

# DEBIAN/control
define CONTROL
Package: linapple
Priority: optional
Section: system
Installed-Size: $(SIZE)
Maintainer: LinApple team <https://github.com/linappleii/linapple/issues>
Architecture: $(ARCH)
Version: $(VERSION)
Depends: libzip-dev, libsdl1.2-dev, libsdl-image1.2-dev, libcurl4-openssl-dev, zlib1g-dev, imagemagick
Homepage: https://github.com/linappleii/linapple
Description: A Linux emulator for Apple ][+, IIe and Enhanced //e with Mockingboard support
endef
export CONTROL

#-------------------------------------------------------------------------------
# DO NOT EDIT BELOW THIS LINE
#-------------------------------------------------------------------------------
SOURCES := $(shell find $(SRCDIR) -type f -name *.$(SRCEXT))
OBJECTS := $(patsubst $(SRCDIR)/%,$(BUILDDIR)/%,$(SOURCES:.$(SRCEXT)=.$(OBJEXT)))
SRCIMGS := $(foreach dir,$(RESDIR),$(wildcard $(dir)/*.$(IMGEXT)))
DSTIMGS := $(patsubst $(RESDIR)/%,$(BUILDDIR)/%,$(SRCIMGS:.$(IMGEXT)=.$(XPMEXT)))
SRCSYMS := $(wildcard $(RESDIR)/*.$(SYMEXT))
DSTSYMS := $(patsubst $(RESDIR)/%,$(TARGETDIR)/%,$(SRCSYMS))

all: resources $(TARGETDIR)/$(TARGET) symbolfiles

remake: distclean all

#Create XPM versions of images
images: $(DSTIMGS) directories

resources: directories images build/$(CONFIGDIR)/linapple.conf build/$(DATADIR)/Master.dsk

build/$(CONFIGDIR)/linapple.conf: $(RESDIR)/linapple.conf
	@cp $< $@

build/$(DATADIR)/Master.dsk: $(RESDIR)/Master.dsk
	@cp $< $@

#Copy symbol files next to binary
symbolfiles: $(DSTSYMS)

directories:
	@mkdir -p $(BUILDDIR) $(TARGETDIR) build/$(DATADIR) build/$(CONFIGDIR)

#Clean only Objects
clean:
	@$(RM) -rf $(BUILDDIR)

#Full Clean, Objects and Binaries
#https://www.gnu.org/prep/standards/html_node/Standard-Targets.html
distclean: clean
	@$(RM) -rf build
	@find . -type f -iname '*~' -exec rm -f {} \;
	@$(RM) $(TARGET)-$(VERSION).deb

#Pull in dependency info for *existing* .o files
-include $(OBJECTS:.$(OBJEXT)=.$(DEPEXT))

#Link
$(TARGETDIR)/$(TARGET): $(OBJECTS)
	$(CC) $(LFLAGS) -o $(TARGETDIR)/$(TARGET) $^ $(LIB)

#Compile
$(BUILDDIR)/%.$(OBJEXT): $(SRCDIR)/%.$(SRCEXT)
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(INC) -c -o $@ $<
	@$(CC) $(CFLAGS) $(INCDEP) -MM $(SRCDIR)/$*.$(SRCEXT) > $(BUILDDIR)/$*.$(DEPEXT)
	@cp -f $(BUILDDIR)/$*.$(DEPEXT) $(BUILDDIR)/$*.$(DEPEXT).tmp
	@sed -e 's|.*:|$(BUILDDIR)/$*.$(OBJEXT):|' < $(BUILDDIR)/$*.$(DEPEXT).tmp > $(BUILDDIR)/$*.$(DEPEXT)
	@sed -e 's/.*://' -e 's/\\$$//' < $(BUILDDIR)/$*.$(DEPEXT).tmp | fmt -1 | sed -e 's/^ *//' -e 's/$$/:/' >> $(BUILDDIR)/$*.$(DEPEXT)
	@rm -f $(BUILDDIR)/$*.$(DEPEXT).tmp

$(BUILDDIR)/%.$(XPMEXT): $(RESDIR)/%.$(IMGEXT)
	convert -flatten "$<" "$@"
	@sed -i 's/static const char/static char/g' "$@"
	@sed -i 's/$(notdir $(basename $@))\[\]/$(notdir $(basename $@))_xpm[]/g' "$@"

$(TARGETDIR)/%.$(SYMEXT): $(RESDIR)/%.$(SYMEXT)
	cp $< $@

install: all
	install -D --target-directory "$(DESTDIR)/$(BINDIR)" build/$(BINDIR)/$(TARGET)
	install -D --target-directory "$(DESTDIR)/$(DATADIR)" build/$(DATADIR)/Master.dsk
	install -D --target-directory "$(DESTDIR)/$(CONFIGDIR)" build/$(CONFIGDIR)/$(PACKAGE).conf

uninstall:
	$(RM) $(DESTDIR)/bin/$(TARGET)
	$(RM) $(DESTDIR)/$(DATADIR)/*
	rmdir $(DESTDIR)/$(DATADIR)

.ONESHELL:
deb:
	@if [ ! -d build ]; then
		@echo "$$COMPILED"
		@exit 0
	@fi
	@echo " Building a Debian package..."
	@rm -rf pkg/
	@mkdir -p $(PKGDIR)/DEBIAN/
	@mkdir -p $(PKGDIR)/$(DESTDIR)/$(BINDIR)
	@echo "$$CONFFILES" > $(PKGDIR)/DEBIAN/conffiles
	@echo "$$CONTROL" > $(PKGDIR)/DEBIAN/control
	@cp -r build/bin/$(TARGET) $(PKGDIR)/$(DESTDIR)/$(BINDIR)
	@cp -r build/etc/ $(PKGDIR)/$(DESTDIR)
	@cp -r build/share/ $(PKGDIR)/$(DESTDIR)
	@cd $(PKGDIR)
	@find . -type d -name "DEBIAN" -prune -o -type f -printf '%P ' | xargs md5sum > DEBIAN/md5sums
	@cd ../..
	@mv $(PKGDIR) $(PKGDIR)_$(VERSION)_$(ARCH)
	@dpkg-deb -b --root-owner-group $(PKGDIR)_$(VERSION)_$(ARCH)
	@mv $(PKGDIR)_$(VERSION)_$(ARCH) $(PKGDIR)
	@mv $(PKGDIR)_$(VERSION)_$(ARCH).deb .

#Non-File Targets
.PHONY: all clean deb directories distclean images install remake resources symbolfiles uninstall
