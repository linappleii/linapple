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

VERSION			:= 2.1
PKG         := linapple

INSTDIR = /usr/local/bin/$(EXE)
CONFDIR = ~/$(TARGET)

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
CFLAGS      := -Wall -O3 -ansi -c
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

#Defauilt Make
all: resources $(TARGET)

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
	mkdir -p "pkg/$(PKG)/$(PKG)-$(VERSION)/etc/$(PKG)"
	mkdir -p "pkg/$(PKG)/$(PKG)-$(VERSION)/usr/bin/"
	@cp -rf $(RESDIR)/* "pkg/$(PKG)/$(PKG)-$(VERSION)/etc/$(PKG)"
	@cp $(TARGETDIR)/$(TARGET) "pkg/$(PKG)/$(PKG)-$(VERSION)/usr/bin/$(TARGET)"
	chown -R root:root "pkg/$(PKG)"
	dpkg --build "pkg/$(PKG)/$(PKG)-$(VERSION)"
	mv "pkg/$(PKG)/$(PKG)-$(VERSION).deb" .

install: all
	@echo " o Creating install directory '$(INSTDIR)'"
	mkdir -p "$(INSTDIR)"
	chmod 777 "$(INSTDIR)"
	@echo " o Creating additional directories 'conf' and 'ftp' in '$(INSTDIR)'"
	mkdir "$(CONFDIR)/conf"
	mkdir -p "$(CONFDIR)/sound"

#Clean only Objecst
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

$(TARGET): $(OBJECTS)
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

