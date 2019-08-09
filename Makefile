# default build configuration
CFG ?= config.mk

include $(CFG)
include proj.mk

# Executable path
BINPATH = $(PREFIX)/bin

# Config path
CFG_PATH = /etc/portmond

# Services path
SRV_PATH = /usr/lib/systemd/system

# Debian Build
BLD_DEB_PATH = /home/pi/portmond-1.0

BLD_DEB_SRC_PATH = /home/pi/portmond-src-1.0

# Manual prefix
MANPREFIX = $(PREFIX)/share/man

# Generate object file names from source file names
# C and C++ objects
COBJ = $(SRC:.c=.${O}) $(CXX_SRC:.cpp=.${O})
# Assembler objects
ASOBJ = $(AS_SRC:.S=.${O})
# All objects
OBJ = $(COBJ) $(ASOBJ)

.PHONY : all debug clean install uninstall

all: $(OUT)

.S.$(O) :
	@echo "AS" $<
	@$(AS) $(ASFLAGS) -o $@ $<

.c.$(O) :
	@echo "CC" $<
	@$(CC) $(CFLAGS) $(DEFINES) -c $<

#gcc -Wall -pthread -o prog prog.c -lpigpio -lrt

.cpp.$(O) :
	@echo "CXX" $<
	@$(CXX) $(CXXFLAGS) $(DEFINES) -c $<

$(COBJ) : $(HEADERS) $(CXX_HEADERS)

$(ASOBJ) : $(AS_INC)

$(OUT) : $(OBJ)
	@echo "LD" $@
	@$(LD) $(LDFLAGS) -o $(OUT) $(OBJ) $(LIBS)

clean :
	@rm -f $(OBJ)
	@rm -f $(OUT) $(OUT).exe

# Debug target
debug :
	@echo "DEBUG build"
	@DEBUG=1 $(MAKE) -s

install : all
	@echo "INSTALL to: $(PREFIX) $(CFG_PATH) $(SRV_PATH)"
	@mkdir -p $(BINPATH) 
	@echo "CP $(OUT) $(BINPATH)"
	@cp -f $(OUT) $(BINPATH)
	@chmod 755 $(BINPATH)/$(OUT)
	@mkdir -p $(CFG_PATH)
	@echo "CP portmond.conf $(CFG_PATH)"
	@cp -f portmon.conf $(CFG_PATH)
	@echo "CP portmond.service $(SRV_PATH)"
	@mkdir -p $(SRV_PATH)
	@cp -f portmond.service $(SRV_PATH)
ifdef STRIP
	@$(STRIP) $(BINPATH)/$(OUT)
endif
# If defined manual files
ifdef MANFILES
	@for i in ${MANFILES} ; \
	do \
		manfile="$$(basename $${i})"; \
		mancat="$${manfile##*.}"; \
		manpath="${MANPREFIX}/man$${mancat}"; \
		\
		echo "CP $${manfile} $${manpath}"; \
		mkdir -p $${manpath}; \
		cp -f $${i} $${manpath}; \
		chmod 644 $${manpath}/$${manfile}; \
	done
endif

uninstall :
	@echo "UNINSTALL from: $(PREFIX) $(CFG_PATH) $(SRV_PATH)"
	@echo "RM $(BINPATH)/$(OUT)"
	@rm -f $(BINPATH)/$(OUT)
	@echo "RM $(CFG_PATH)/portmond.conf"
	@rm -f $(CFG_PATH)/portmon.conf
	@echo "RM $(SRV_PATH)/portmond.service"
	@rm -f $(SRV_PATH)/portmond.sevice
# If defined manual files
ifdef MANFILES
	@for i in ${MANFILES} ; \
	do \
		manfile="$$(basename $${i})"; \
		mancat="$${manfile##*.}"; \
		manpath="${MANPREFIX}/man$${mancat}"; \
		\
		echo "RM $${manpath}/$${manfile}"; \
		rm -f $${manpath}/$${manfile}; \
	done
endif

build_deb :
	echo "Building file structure in $(BLD_DEB_PATH)"
	@mkdir -p $(BLD_DEB_PATH)
	@mkdir -p $(BLD_DEB_PATH)/DEBIAN
	@mkdir -p $(BLD_DEB_PATH)/usr
	@mkdir -p $(BLD_DEB_PATH)/usr/bin
	@mkdir -p $(BLD_DEB_PATH)/usr/lib
	@mkdir -p $(BLD_DEB_PATH)/usr/lib
	@mkdir -p $(BLD_DEB_PATH)/usr/lib/systemd
	@mkdir -p $(BLD_DEB_PATH)/usr/lib/systemd/system
	@mkdir -p $(BLD_DEB_PATH)/etc/portmond
	@echo "Copying files to $(BLD_DEB_PATH)"
	@cp -f $(OUT) $(BLD_DEB_PATH)/usr/bin
ifdef STRIP
	@$(STRIP) $(BLD_DEB_PATH)/usr/bin/$(OUT)
endif
	@cp -f portmon.conf $(BLD_DEB_PATH)/etc/portmond
	@cp -f portmon.txt $(BLD_DEB_PATH)/etc/portmond
	@cp -f portmond.service $(BLD_DEB_PATH)/usr/lib/systemd/system
	@cp -f DEBIAN/control $(BLD_DEB_PATH)/DEBIAN
	@chown -R root:root $(BLD_DEB_PATH)/etc
	@chown -R root:root $(BLD_DEB_PATH)/usr
	@chown -R root:root $(BLD_DEB_PATH)/DEBIAN
	@dpkg -b /home/pi/portmond-1.0
	@rm -rf $(BLD_DEB_PATH)

build_deb_src :
	echo "Building file structure in $(BLD_DEB_SRC_PATH)"
	@mkdir $(BLD_DEB_SRC_PATH)
	@mkdir $(BLD_DEB_SRC_PATH)/DEBIAN
	@mkdir $(BLD_DEB_SRC_PATH)/home
	@mkdir $(BLD_DEB_SRC_PATH)/home/pi
	@mkdir $(BLD_DEB_SRC_PATH)/home/pi/portmond
	@chown pi:pi $(BLD_DEB_SRC_PATH)
	@chown -R pi:pi $(BLD_DEB_SRC_PATH)/home/pi
	@echo "Copying files to $(BLD_DEB_SRC_PATH)"
	@cp -fpr * $(BLD_DEB_SRC_PATH)/home/pi/portmond
	@cp -f DEBIAN/control.src $(BLD_DEB_SRC_PATH)/DEBIAN/control
	@dpkg -b /home/pi/portmond-src-1.0
	@rm -rf $(BLD_DEB_SRC_PATH)