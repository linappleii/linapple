CXX ?= c++
CXXFLAGS ?= -O3
CXXFLAGS += -w

OBJS := $(filter-out Applewin.cpp Util_MemoryTextFile.cpp,$(wildcard *.cpp))
OBJS := $(OBJS:.cpp=.o)

SDL_CONFIG ?= sdl-config
SDL_CFLAGS = $(shell $(SDL_CONFIG) --cflags)
SDL_LIBS = $(shell $(SDL_CONFIG) --libs)

CURL_CONFIG ?= curl-config
CURL_CFLAGS = $(shell $(CURL_CONFIG) --cflags)
CURL_LIBS = $(shell $(CURL_CONFIG) --libs)

# build with `RESOURCE_DIR=. make` to get the legacy behaviour of loading resource files from the current directory
RESOURCE_DIR ?= $(shell (cd .. && pwd))

CXXFLAGS += $(SDL_CFLAGS)
CXXFLAGS += $(CURL_CFLAGS)
CXXFLAGS += -DRESOURCE_DIR=\"$(RESOURCE_DIR)/\"
LDLIBS += $(SDL_LIBS) $(CURL_LIBS) -lz -lzip

# Define executable and installing directory
EXE = linapple
INSTDIR = /usr/local/$(EXE)
STARTUP = /usr/local/bin/$(EXE)
DATA = ../charset40.bmp ../font.bmp ../icon.bmp ../splash.bmp ../Master.dsk ../linapple.installed.conf

.PHONY: all clean install uninstall

all: $(EXE)

Applewin: $(OBJS)

$(EXE): Applewin
#	$(CC) $(CFLAGS) $(LDFLAGS) $^ -o $@
	mv $^ $@
	strip $@
	@echo " o $(EXE) compiled succesfully"
	cp $@ ../

clean:
	rm -f $(EXE) *.o

#
# Installation routines
#

install: all
	@echo " o Creating install directory '$(INSTDIR)'"
	mkdir "$(INSTDIR)"
	@echo " o Creating additional directories 'images' and 'ftp' in '$(INSTDIR)'"
	mkdir "$(INSTDIR)/images"
	mkdir -p "$(INSTDIR)/ftp/cache"

	@echo " o Installing game and data to '$(INSTDIR)'"
	cp $(EXE) $(DATA) "$(INSTDIR)"

	@echo " o Preparing configuration file 'linapple.conf'"
	mv "$(INSTDIR)/linapple.installed.conf" "$(INSTDIR)/linapple.conf"
	chmod -R 777 "$(INSTDIR)/"

	@echo " o Creating startup script $(STARTUP)"
	echo "cd \"$(INSTDIR)\"; ./$(EXE) \$$1 \$$2 \$$3 \$$4 \$$5 \$$6; cd -" >"$(STARTUP)"
	chmod 755 "$(STARTUP)"
	
	@echo ""
	@echo "Type $(EXE) to start the beast!"
	@echo "To change some settings edit \"$(INSTDIR)/linapple.conf\""
  
uninstall:
	@echo " o Removing ALL data from $(INSTDIR)"
	rm -rf "$(INSTDIR)"
	@echo " o Removing startup script $(STARTUP)"
	rm -f "$(STARTUP)"
	@echo " o All done! Linapple has been successfully removed from your system"
