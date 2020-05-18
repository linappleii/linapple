#! /usr/bin/make -f

# TODO conform to
# https://www.gnu.org/prep/standards/html_node/Standard-Targets.html

PACKAGE     := linapple
VERSION     := 2.1.1

#Compiler and Linker
CC          := g++

# Where does this get installed
PREFIX      ?= /usr/local

# Where assets (font file, etc) are read from
# (You can build a version that loads assets from the source tree with: DATADIR=`pwd`/res make )
DATADIR     ?= share/$(PACKAGE)

# Build and installation staging location.
DESTDIR     := build

# Where default versions of resource files (linapple.conf, etc) are obtained from initially
# (You can build a version that copies resources from the source tree with: RESOURCE_INIT_DIR=`pwd`/res make )
RESOURCE_INIT_DIR ?= $(DATADIR)

PKGDIR="pkg/$(PACKAGE)/$(PACKAGE)-$(VERSION)"

#The Target Binary Program
TARGET      := linapple

#The Directories, Source, Includes, Objects, Binary and Resources
SRCDIR      := src
INCDIR      := inc
BINDIR       = bin
OBJDIR       = obj
BUILDDIR    := $(DESTDIR)/$(OBJDIR)
TARGETDIR   := $(DESTDIR)/$(BINDIR)
RESDIR      := res
SRCEXT      := cpp
DEPEXT      := d
OBJEXT      := o
IMGEXT      := png
XPMEXT      := xpm
SYMEXT      := SYM

#Flags, Libraries and Includes

SDL_CONFIG ?= sdl-config
SDL_CFLAGS = $(shell $(SDL_CONFIG) --cflags)
SDL_LIBS = $(shell $(SDL_CONFIG) --libs)
SDL_LIBS +=  $(shell pkg-config SDL_image --libs)

CURL_CONFIG ?= curl-config
CURL_CFLAGS = $(shell $(CURL_CONFIG) --cflags)
CURL_LIBS = $(shell $(CURL_CONFIG) --libs)

# By default, optimize the executable.
CFLAGS := -Wall -O3 -ansi -c -std=c++11

ifdef PROFILING
CFLAGS := -Wall -O0 -pg -ggdb -ansi -c
LFLAGS := -pg
endif

ifdef DEBUG
CFLAGS := -Wall -O0 -ggdb -ansi -c -finstrument-functions -std=c++11
endif

CFLAGS += -DASSET_DIR=\"$(DATADIR)\" -DRESOURCE_INIT_DIR=\"$(RESOURCE_INIT_DIR)\" -DVERSIONSTRING=\"$(VERSION)\"
CFLAGS += $(SDL_CFLAGS)
CFLAGS += $(CURL_CFLAGS)
# Do not complain about XPMs
CFLAGS += -Wno-write-strings

LIB    := $(SDL_LIBS) $(CURL_LIBS) -lz -lzip -pthread -lX11
INC    := -I$(INCDIR) -I/usr/local/include
INCDEP := -I$(INCDIR)

#------------------------------------------------------------------------------
#DO NOT EDIT BELOW THIS LINE
# i'm going to. -rhaleblian
#------------------------------------------------------------------------------
SOURCES := $(shell find $(SRCDIR) -type f -name *.$(SRCEXT))
OBJECTS := $(patsubst $(SRCDIR)/%,$(BUILDDIR)/%,$(SOURCES:.$(SRCEXT)=.$(OBJEXT)))
SRCIMGS := $(foreach dir,$(RESDIR),$(wildcard $(dir)/*.$(IMGEXT)))
DSTIMGS := $(patsubst $(RESDIR)/%,$(BUILDDIR)/%,$(SRCIMGS:.$(IMGEXT)=.$(XPMEXT)))
SRCSYMS := $(wildcard $(RESDIR)/*.$(SYMEXT))
DSTSYMS := $(patsubst $(RESDIR)/%,$(TARGETDIR)/%,$(SRCSYMS))

all: resources $(TARGETDIR)/$(TARGET) symbolfiles

remake: distclean all

# Create XPM versions of images.
images: $(DSTIMGS) directories

resources: directories images
	@cp $(RESDIR)/linapple.conf $(DESTDIR)/$(DATADIR)
	@cp $(RESDIR)/Master.dsk $(DESTDIR)/$(DATADIR)

# Copy symbol files next to binary
symbolfiles: $(DSTSYMS)

directories:
	@mkdir -p $(BUILDDIR) $(TARGETDIR) $(DESTDIR)/$(DATADIR)

#Clean only Objects
clean:
	@$(RM) -rf $(BUILDDIR)
	@$(RM) -rf $(TARGETDIR)

#Full Clean, Objects and Binaries
cleaner: clean
	@$(RM) -rf $(TARGETDIR)
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
	@sed -i 's/$(notdir $(basename $@))\[\]/$(notdir $(basename $@))_xpm[]/g' "$@"

$(TARGETDIR)/%.$(SYMEXT): $(RESDIR)/%.$(SYMEXT)
	cp $< $@

# https://www.gnu.org/prep/standards/html_node/Standard-Targets.html
distclean: cleaner
	rm -rf $(DESTDIR)

install: all
	install -d "$(PREFIX)/$(BINDIR)"
	install $(DESTDIR)/$(BINDIR)/$(TARGET) "$(PREFIX)/$(BINDIR)/$(TARGET)"
	install -d "$(PREFIX)/$(DATADIR)"
	install $(DESTDIR)/$(DATADIR)/* "$(PREFIX)/$(DATADIR)"

uninstall:
	$(RM) $(PREFIX)/bin/$(TARGET)
	$(RM) $(PREFIX)/$(DATADIR)/*
	rmdir $(PREFIX)/$(DATADIR)

$(PACKAGE)/$(PACKAGE)-$(VERSION).deb:
	@echo " Building a Debian package ..."
	rm -rf $(PKGDIR)
	mkdir -p $(PKGDIR)/$(BINDIR)
	cp $(DESTDIR)/$(BINDIR)/$(TARGET)/$(PKGDIR)/$(BINDIR)
	mkdir -p $(PKGDIR)/$(DATADIR) 
	cp -r $(DESTDIR)/$(DATADIR) $(PKGDIR)/$(DATADIR)
	dpkg --build $(PKGDIR)
	mv $(PACKAGE)/$(PACKAGE)-$(VERSION).deb" .
package: $(PACKAGE)/$(PACKAGE)-$(VERSION).deb

#Non-File Targets
.PHONY: all clean cleaner deb directories distclean images install package remake resources symbolfiles
