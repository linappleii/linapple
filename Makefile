# Makefile for linapple
# A Linux emulator for Apple ][+, IIe and Enhanced //e
#
# Standards: GNU Makefile Standards
# Features: Automatic dependency tracking, pkg-config, verbosity control

SHELL := /bin/sh

# --- Configuration -----------------------------------------------------------

PACKAGE     := linapple
VERSION     := 3.0.0
TARGET      := linapple

# --- Installation Paths ------------------------------------------------------

prefix      ?= /usr/local
exec_prefix := $(prefix)
bindir      := $(exec_prefix)/bin
datarootdir := $(prefix)/share
datadir     := $(datarootdir)/$(PACKAGE)
sysconfdir  := $(prefix)/etc/$(PACKAGE)

# DESTDIR is used for staged installs (e.g. package building)
DESTDIR     ?=

# --- Tools -------------------------------------------------------------------

CXX             := g++
INSTALL         := install
INSTALL_PROGRAM := $(INSTALL)
INSTALL_DATA    := $(INSTALL) -m 644
RM              := rm -rf
PKG_CONFIG      := pkg-config

# --- Verbosity Control -------------------------------------------------------

ifeq ($(V),1)
  Q :=
else
  Q := @
endif

# --- Directories -------------------------------------------------------------

SRCDIR      := src
INCDIR      := inc
RESDIR      := res
BUILD_ROOT  := build
OBJDIR      := $(BUILD_ROOT)/obj
BINDIR      := $(BUILD_ROOT)/bin

# --- Flags & Libraries -------------------------------------------------------

# Library detection via pkg-config
SDL_CFLAGS  := $(shell $(PKG_CONFIG) --cflags sdl3 sdl3-image)
SDL_LIBS    := $(shell $(PKG_CONFIG) --libs sdl3 sdl3-image)
CURL_CFLAGS := $(shell $(PKG_CONFIG) --cflags libcurl 2>/dev/null || curl-config --cflags)
CURL_LIBS   := $(shell $(PKG_CONFIG) --libs libcurl 2>/dev/null || curl-config --libs)

# Preprocessor flags
CPPFLAGS += -I$(INCDIR) -I/usr/local/include
CPPFLAGS += -DASSET_DIR=\"$(datadir)\" -DVERSIONSTRING=\"$(VERSION)\"
CPPFLAGS += $(SDL_CFLAGS) $(CURL_CFLAGS)

# Compiler flags
CXXFLAGS += -Wall -Wextra -O3 -march=native -flto -ansi -std=c++11 -Wno-write-strings
CXXFLAGS += -MMD -MP

# Linker flags
LDFLAGS  += -pthread -flto
LDLIBS   += $(SDL_LIBS) $(CURL_LIBS) -lz -lzip -lX11

# --- Build Modes -------------------------------------------------------------

ifdef PROFILING
  CXXFLAGS := -Wall -O0 -pg -ggdb -ansi -std=c++11 -MMD -MP
  LDFLAGS  += -pg
endif

ifdef DEBUG
  CXXFLAGS := -O0 -ggdb -ansi -finstrument-functions -std=c++11 -Wno-write-strings -Werror -MMD -MP
endif

ifdef REGISTRY_WRITEABLE
  CPPFLAGS += -DREGISTRY_WRITEABLE=1
endif

# --- Source Discovery --------------------------------------------------------

SOURCES := $(shell find $(SRCDIR) -type f -name "*.cpp")
OBJECTS := $(SOURCES:$(SRCDIR)/%.cpp=$(OBJDIR)/%.o)
DEPS    := $(OBJECTS:.o=.d)

# Assets and resources
SRCIMGS := $(wildcard $(RESDIR)/*.png)
DSTIMGS := $(SRCIMGS:$(RESDIR)/%.png=$(OBJDIR)/%.xpm)
SRCSYMS := $(wildcard $(RESDIR)/*.SYM)
DSTSYMS := $(SRCSYMS:$(RESDIR)/%.SYM=$(BINDIR)/%.SYM)

# --- Standard Targets --------------------------------------------------------

.PHONY: all help install install-strip uninstall mostlyclean clean distclean \
        maintainer-clean check installcheck dist info html pdf ps \
        install-info install-html install-pdf install-ps installdirs

all: resources $(BINDIR)/$(TARGET) symbolfiles ## Build the application (default)

help: ## Show this help message
	@grep -E '^[a-zA-Z_-]+:.*?## .*$$' $(MAKEFILE_LIST) | sort | awk 'BEGIN {FS = ":.*?## "}; {printf "\033[36m%-20s\033[0m %s\n", $$1, $$2}'

installdirs: ## Create installation directories
	$(Q)mkdir -p "$(DESTDIR)$(bindir)"
	$(Q)mkdir -p "$(DESTDIR)$(datadir)"
	$(Q)mkdir -p "$(DESTDIR)$(sysconfdir)"

install: all installdirs ## Install the application
	@echo "  INSTALL $(TARGET)"
	$(Q)$(INSTALL_PROGRAM) $(BINDIR)/$(TARGET) "$(DESTDIR)$(bindir)/$(TARGET)"
	$(Q)$(INSTALL_DATA) $(BUILD_ROOT)/share/$(PACKAGE)/Master.dsk "$(DESTDIR)$(datadir)/Master.dsk"
	$(Q)$(INSTALL_DATA) $(BUILD_ROOT)/etc/$(PACKAGE)/$(PACKAGE).conf "$(DESTDIR)$(sysconfdir)/$(PACKAGE).conf"

install-strip: ## Install stripped version of the binary
	$(Q)$(MAKE) INSTALL_PROGRAM="$(INSTALL_PROGRAM) -s" install

uninstall: ## Uninstall the application
	@echo "  UNINSTALL"
	$(Q)$(RM) "$(DESTDIR)$(bindir)/$(TARGET)"
	$(Q)$(RM) "$(DESTDIR)$(datadir)"
	$(Q)$(RM) "$(DESTDIR)$(sysconfdir)"

mostlyclean: clean

clean: ## Remove object files and temporary build artifacts
	@echo "  CLEAN"
	$(Q)$(RM) $(OBJDIR)

distclean: clean ## Remove all build artifacts and generated files
	@echo "  DISTCLEAN"
	$(Q)$(RM) $(BUILD_ROOT)
	$(Q)$(RM) pkg
	$(Q)find . -type f -iname '*~' -exec rm -f {} \;
	$(Q)$(RM) $(TARGET)-$(VERSION).deb
	$(Q)$(RM) $(TARGET)-$(VERSION).tar.gz

maintainer-clean: distclean
	@echo "Cleaning for maintainers..."

check: test-cpu test-integration ## Run all tests

test-cpu: all ## Run automated CPU functional tests
	@bash scripts/run_cpu_tests.sh

test-integration: all ## Run C++ clean-room hardware integration tests
	@echo "  TEST    build/bin/test_integration"
	$(Q)$(CXX) $(CXXFLAGS) $(CPPFLAGS) -Itests tests/test_main.cpp $(filter-out %/Applewin.o,$(OBJECTS)) $(LDLIBS) -o build/bin/test_integration
	@./build/bin/test_integration

installcheck: ## Run tests on installed program
	@echo "No installation tests defined."

dist: distclean ## Create a distribution tarball
	@echo "  DIST $(TARGET)-$(VERSION).tar.gz"
	$(Q)tar -cvzf $(TARGET)-$(VERSION).tar.gz --exclude=$(TARGET)-$(VERSION).tar.gz .

# Documentation placeholders
info html pdf ps:
install-info install-html install-pdf install-ps:

# --- Build Logic -------------------------------------------------------------

remake: distclean all ## Clean and rebuild everything

resources: directories $(DSTIMGS) $(BUILD_ROOT)/etc/$(PACKAGE)/$(PACKAGE).conf $(BUILD_ROOT)/share/$(PACKAGE)/Master.dsk

$(BUILD_ROOT)/etc/$(PACKAGE)/$(PACKAGE).conf: $(RESDIR)/linapple.conf
	$(Q)cp $< $@

$(BUILD_ROOT)/share/$(PACKAGE)/Master.dsk: $(RESDIR)/Master.dsk
	$(Q)cp $< $@

symbolfiles: $(DSTSYMS)

directories:
	$(Q)mkdir -p $(OBJDIR) $(BINDIR) $(BUILD_ROOT)/share/$(PACKAGE) $(BUILD_ROOT)/etc/$(PACKAGE)

$(BINDIR)/$(TARGET): $(OBJECTS)
	@echo "  LD      $@"
	$(Q)$(CXX) $(LDFLAGS) -o $@ $^ $(LDLIBS)

# Pattern rule for objects with dependency generation
$(OBJDIR)/%.o: $(SRCDIR)/%.cpp | directories resources
	@mkdir -p $(dir $@)
	@echo "  CXX     $<"
	$(Q)$(CXX) $(CPPFLAGS) $(CXXFLAGS) -c -o $@ $<

# Asset conversion (PNG to XPM)
$(OBJDIR)/%.xpm: $(RESDIR)/%.png | directories
	@echo "  XPM     $<"
	$(Q)magick "$<" -layers flatten "$@"
	$(Q)sed -i 's/static const char/static char/g' "$@"
	$(Q)sed -i 's/$(notdir $(basename $@))\[\]/$(notdir $(basename $@))_xpm[]/g' "$@"


$(BINDIR)/%.SYM: $(RESDIR)/%.SYM | directories
	$(Q)cp $< $@

-include $(DEPS)

# --- Packaging ---------------------------------------------------------------

.ONESHELL:
deb: ## Build a Debian package
	@if [ ! -d $(BUILD_ROOT) ]; then \
		echo "Error: build directory missing. Run 'make' first."; \
		exit 1; \
	fi
	@echo " Building Debian package..."
	$(Q)$(RM) pkg/
	PKGDIR="pkg/$(PACKAGE)"
	ARCH="all"
	SIZE=$$(du -s $(BUILD_ROOT) | cut -f 1)
	mkdir -p $$PKGDIR/DEBIAN/
	mkdir -p $$PKGDIR/$(bindir)
	echo "$(sysconfdir)/$(PACKAGE).conf" > $$PKGDIR/DEBIAN/conffiles
	echo "$(datadir)/Master.dsk" >> $$PKGDIR/DEBIAN/conffiles
	cat <<-EOF > $$PKGDIR/DEBIAN/control
	Package: $(PACKAGE)
	Priority: optional
	Section: system
	Installed-Size: $$SIZE
	Maintainer: LinApple team <https://github.com/linappleii/linapple/issues>
	Architecture: $$ARCH
	Version: $(VERSION)
	Depends: libzip-dev, libsdl1.2-dev, libsdl-image1.2-dev, libcurl4-openssl-dev, zlib1g-dev, imagemagick
	Homepage: https://github.com/linappleii/linapple
	Description: A Linux emulator for Apple ][+, IIe and Enhanced //e with Mockingboard support
	EOF
	cp -r $(BINDIR)/$(TARGET) $$PKGDIR/$(bindir)
	mkdir -p $$PKGDIR/$(sysconfdir)
	cp -r $(BUILD_ROOT)/etc/$(PACKAGE)/* $$PKGDIR/$(sysconfdir)
	mkdir -p $$PKGDIR/$(datadir)
	cp -r $(BUILD_ROOT)/share/$(PACKAGE)/* $$PKGDIR/$(datadir)
	cd $$PKGDIR
	find . -type d -name "DEBIAN" -prune -o -type f -printf '%P ' | xargs md5sum > DEBIAN/md5sums
	cd ../..
	mv $$PKGDIR $${PKGDIR}_$(VERSION)_$$ARCH
	dpkg-deb -b --root-owner-group $${PKGDIR}_$(VERSION)_$$ARCH
	mv $${PKGDIR}_$(VERSION)_$$ARCH $$PKGDIR
	mv $${PKGDIR}_$(VERSION)_$$ARCH.deb .
