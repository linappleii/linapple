CXX ?= c++
CXXFLAGS ?= -O3
CXXFLAGS += -Wall

OBJS := $(filter-out Applewin.cpp Util_MemoryTextFile.cpp,$(wildcard *.cpp))
OBJS := $(OBJS:.cpp=.o)

SDL_CONFIG ?= sdl-config
SDL_CFLAGS = $(shell $(SDL_CONFIG) --cflags)
SDL_LIBS = $(shell $(SDL_CONFIG) --libs)

CURL_CONFIG ?= curl-config
CURL_CFLAGS = $(shell $(CURL_CONFIG) --cflags)
CURL_LIBS = $(shell $(CURL_CONFIG) --libs)

CXXFLAGS += $(SDL_CFLAGS)
CXXFLAGS += $(CURL_CFLAGS)
LDLIBS += $(SDL_LIBS) $(CURL_LIBS) -lz -lzip

# Define executable and installing directory
EXE = linapple
INSTDIR = /usr/local/$(EXE)
STARTUP = /usr/local/bin/$(EXE)
DATA = ../charset40.bmp ../font.bmp ../icon.bmp ../splash.bmp ../Master.dsk ../linapple.installed.conf

#all: ../linapple
all: $(EXE)

Applewin: $(OBJS)

$(EXE): Applewin
#	$(CC) $(CFLAGS) $(LDFLAGS) $^ -o $@
	mv $^ $@
	strip $@
	@echo " o If there are no errors, $(EXE) compiled succesfully"
	cp $@ ../

# static binary: (DOES NOT WORK)
#static: $(OBJS)
#	$(CC) -static $^ -o $@ `sdl-config --static-libs` -ldl -lz
#	strip static
#	mv static ../$(EXE)-static

clean:
	rm -f $(EXE) *.o

.PHONY: all clean

# Installation routines

install: all
	@echo " o Creating install directory '$(INSTDIR)'"
	mkdir "$(INSTDIR)"
	chmod 777 "$(INSTDIR)"
	@echo " o Creating additional directories 'images' and 'ftp' in '$(INSTDIR)'"
	mkdir "$(INSTDIR)/images"
	mkdir -p "$(INSTDIR)/ftp/cache"

	@echo " o Installing game and data to '$(INSTDIR)'"
	cp $(EXE) $(DATA) "$(INSTDIR)"

	@echo " o Preparing configuration file 'linapple.conf'"
	mv "$(INSTDIR)/linapple.installed.conf" "$(INSTDIR)/linapple.conf"
	chmod 777 -R "$(INSTDIR)/"

	@echo " o Configuring access permissions for 'ftp' and 'images' directories"
	chmod 777 "$(INSTDIR)/images"
	chmod 777 "$(INSTDIR)/ftp"
	chmod 777 "$(INSTDIR)/ftp/cache"

	@echo " o Creating startup script $(STARTUP)"
	echo "cd \"$(INSTDIR)\"; ./$(EXE) \$$1 \$$2 \$$3 \$$4 \$$5 \$$6; cd -" >"$(STARTUP)"
	chmod 755 "$(STARTUP)"
	
	@echo ""
	@echo "Type '$(EXE)' (no quotes) to start the beast!"
	@echo "To change some settings edit \"$(INSTDIR)/linapple.conf\""
  
uninstall:
	@echo " o Removing ALL data from $(INSTDIR)"
	rm -rf "$(INSTDIR)"
	@echo " o Removing startup script $(STARTUP)"
	rm -f "$(STARTUP)"
	@echo " o All done!"

