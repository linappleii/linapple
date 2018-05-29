#! /usr/bin/make -f

PACKAGE     := linapple
VERSION     := 2.1.1

# Where does this get installed
PREFIX      := /usr/local
ASSET_DIR   := $(PREFIX)/share/$(PACKAGE)

#Compiler and Linker
CC          := g++

#The Target Binary Program
TARGET      := linapple

#The Directories, Source, Includes, Objects, Binary and Resources
SRCDIR      := src
INCDIR      := inc
BUILDDIR    := obj
TARGETDIR   := bin
RESDIR      := res
SRCEXT      := cpp
DEPEXT      := d
OBJEXT      := o

# FIXME: Make this go away!
INSTDIR     := $(PREFIX)/lib/$(PACKAGE)

#Flags, Libraries and Includes

SDL_CONFIG ?= sdl-config
SDL_CFLAGS = $(shell $(SDL_CONFIG) --cflags)
SDL_LIBS = $(shell $(SDL_CONFIG) --libs)

CURL_CONFIG ?= curl-config
CURL_CFLAGS = $(shell $(CURL_CONFIG) --cflags)
CURL_LIBS = $(shell $(CURL_CONFIG) --libs)

#PROFILING
#CFLAGS      := -Wall -O0 -pg -ggdb -ansi -c
#LFLAGS      := -pg
#DEBUGGING
#CFLAGS      := -Wall -O0 -ggdb -ansi -c -finstrument-functions
#OPTIMIZED
CFLAGS      := -Wall -O3 -ansi -c -DASSET_DIR=\"$(ASSET_DIR)\"
CFLAGS 		+= $(SDL_CFLAGS)
CFLAGS 		+= $(CURL_CFLAGS)

LIB 				:= $(SDL_LIBS) $(CURL_LIBS) -lz -lzip
INC         := -I$(INCDIR) -I/usr/local/include
INCDEP      := -I$(INCDIR)

#---------------------------------------------------------------------------------
#DO NOT EDIT BELOW THIS LINE
#---------------------------------------------------------------------------------
SOURCES     := $(shell find $(SRCDIR) -type f -name *.$(SRCEXT))
OBJECTS     := $(patsubst $(SRCDIR)/%,$(BUILDDIR)/%,$(SOURCES:.$(SRCEXT)=.$(OBJEXT)))
INSTASSETS  := \
	charset40.bmp \
	font.bmp \
	icon.bmp \
	splash.bmp \
	Master.dsk
CONFFILES   := \
	linapple.conf

#Default Make
all: resources $(TARGETDIR)/$(TARGET)

#Remake
remake: cleaner all

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

#Non-File Targets
.PHONY: all remake clean cleaner resources

