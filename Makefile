#! /usr/bin/make -f

PACKAGE     := linapple
VERSION     := 2.1.1
# DEBUG       := 1

# Where does this get installed
PREFIX      := /usr/local

# Where assets (font file, etc) are read from
# (You can build a version that loads assets from the source tree with: ASSET_DIR=`pwd`/res make )
ASSET_DIR   ?= $(PREFIX)/share/$(PACKAGE)

# Where default versions of resource files (linapple.conf, etc) are obtained from initially
# (You can build a version that copies resources from the source tree with: RESOURCE_INIT_DIR=`pwd`/res make )
RESOURCE_INIT_DIR ?= /etc/linapple

#Compiler and Linker
CC          := g++

#The Target Binary Program
TARGET      := linapple

#The Directories, Source, Includes, Objects, Binary and Resources
SRCDIR      := src
INCDIR      := inc
BUILDDIR    := build
TARGETDIR   := bin
RESDIR      := res
SRCEXT      := cpp
DEPEXT      := d
OBJEXT      := o
IMGEXT      := png
XPMEXT      := xpm

# FIXME: Make this go away!
INSTDIR     := $(PREFIX)/lib/$(PACKAGE)

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

CFLAGS += -DASSET_DIR=\"$(ASSET_DIR)\" -DRESOURCE_INIT_DIR=\"$(RESOURCE_INIT_DIR)\"
CFLAGS += $(SDL_CFLAGS)
CFLAGS += $(CURL_CFLAGS)
# Do not complain about XPMs
CFLAGS += -Wno-write-strings

LIB    := $(SDL_LIBS) $(CURL_LIBS) -lz -lzip -pthread -lX11
INC    := -I$(INCDIR) -I/usr/local/include
INCDEP := -I$(INCDIR)

#---------------------------------------------------------------------------------
#DO NOT EDIT BELOW THIS LINE
#---------------------------------------------------------------------------------
SOURCES     := $(shell find $(SRCDIR) -type f -name *.$(SRCEXT))
OBJECTS     := $(patsubst $(SRCDIR)/%,$(BUILDDIR)/%,$(SOURCES:.$(SRCEXT)=.$(OBJEXT)))
INSTASSETS  := \
	Master.dsk
CONFFILES   := \
	linapple.conf
SRCIMGFILES := $(foreach dir,$(RESDIR),$(wildcard $(dir)/*.$(IMGEXT)))
DSTIMGFILES := $(SRCIMGFILES:.$(IMGEXT)=.$(XPMEXT))

#Default Make
all: images directories $(TARGETDIR)/$(TARGET)

#Remake
remake: cleaner all

images: $(DSTIMGFILES)

#Copy Resources from Resources Directory to Target Directory
resources: directories
	@cp $(RESDIR)/* $(TARGETDIR)/

#Make the Directories
directories:
	@mkdir -p $(TARGETDIR)
	@mkdir -p $(BUILDDIR)

package: all
	@echo " Building a package"
	mkdir -p "pkg/$(PACKAGE)/$(PACKAGE)-$(VERSION)/etc/$(PACKAGE)"
	mkdir -p "pkg/$(PACKAGE)/$(PACKAGE)-$(VERSION)/usr/bin/"
	@cp -rf $(RESDIR)/* "pkg/$(PACKAGE)/$(PACKAGE)-$(VERSION)/etc/$(PACKAGE)"
	@cp $(TARGETDIR)/$(TARGET) "pkg/$(PACKAGE)/$(PACKAGE)-$(VERSION)/usr/bin/$(TARGET)"
	chown -R root:root "pkg/$(PACKAGE)"
	dpkg --build "pkg/$(PACKAGE)/$(PACKAGE)-$(VERSION)"
	mv "pkg/$(PACKAGE)/$(PACKAGE)-$(VERSION).deb" .

install: all
	@echo "`tput bold`o Creating '$(INSTDIR)'`tput sgr0`"
	# Windows-style all-in-one dir. Let's get rid of this at some point
	install -d -m 755 -o root -g root "$(INSTDIR)"

	@echo
	@echo "`tput bold`o Copying binary to install directory '$(INSTDIR)'`tput sgr0`"
	# This should be able to live in $(PREFIX)/bin directly, symlink for now
	install -m 755 -o root -g root "$(TARGETDIR)/$(TARGET)" "$(INSTDIR)/$(TARGET)"
	# We'll use a symlink until then
	if [ -L "$(PREFIX)/bin/$(TARGET)" ]; then \
		rm -f "$(PREFIX)/bin/$(TARGET)" ;\
	fi
	ln -s $(INSTDIR)/$(TARGET) $(PREFIX)/bin/$(TARGET)

	@echo
	@echo "`tput bold`o Copying assets to '$(ASSET_DIR)'`tput sgr0`"
	install -d -m 755 -o root -g root "$(ASSET_DIR)"
	for file in $(INSTASSETS); do \
		install -m 644 -o root -g root "$(RESDIR)/$$file" "$(ASSET_DIR)/$$file" ;\
	done

	@echo
	@echo "`tput bold`o Copying docs to install directory '$(INSTDIR)'`tput sgr0`"
	# This belongs in $(PREFIX)/etc or /etc
	for file in $(CONFFILES); do \
		install -m 644 -o root -g root "$(RESDIR)/$$file" "$(INSTDIR)/$$file" ;\
	done

uninstall:
	@echo "`tput bold`o Uninstalling $(TARGET) from '$(INSTDIR)'`tput sgr0`"
	# We could possibly just rm -rf this, but that's kind of a no-no
	for file in $(TARGET) $(INSTASSETS) $(CONFFILES); do \
		rm -f "$(INSTDIR)/$$file" ;\
	done
	for file in $(INSTASSETS); do \
		rm -f "$(ASSET_DIR)/$$file" ;\
	done
	# It's okay if these fail (examine the directories yourself)
	rmdir $(INSTDIR) 2>/dev/null || true
	rmdir $(ASSET_DIR) 2>/dev/null || true
	# Don't forget the linapple symlink in $(PREFIX)/bin
	rm -f "$(PREFIX)/bin/$(TARGET)"


#Clean only Objects
clean:
	@$(RM) -rf $(BUILDDIR)
	@$(RM) -rf $(TARGETDIR)
	@$(RM) -f $(RESDIR)/splash.xpm $(RESDIR)/charset*.xpm

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

%.$(XPMEXT): %.$(IMGEXT)
	convert -flatten "$<" "$@"
	@sed -i 's/${$@:.$(IMGEXT)=}\[\]/${file}_xpm[]/g' "$@"

#Non-File Targets
.PHONY: all remake clean cleaner resources

