# BME680 BSEC Lib
BSEC_DIR= ./src/BSEC_1.4.7.4_Generic_Release
ARCH=${VERSION}/bin/RaspberryPI/PiThree_ArmV8-a-64bits
# Other architectures can be found in BSEC_DIR/algo/${VERSION}/bin/.
# Project dependency libraries
LIBS = ${BSEC_DIR}/algo/${ARCH}/libalgobsec.a

# Install prefix
PREFIX ?= /usr

# Object file suffix
O = o
# dll suffix
SO = so
# static lib suffix
A = a

VERSION='normal_version'

CONFIG='generic_33v_3s_4d'

# Other configs are:
# generic_18v_300s_28d
# generic_18v_300s_4d
# generic_18v_3s_28d
# generic_18v_3s_4d
# generic_33v_300s_28d
# generic_33v_300s_4d
# generic_33v_3s_28d
# generic_33v_3s_4d

# C compiler
CC ?= cc
# C++ compiler
CXX ?= g++
# linker
LD = $(CC)
# assembler
AS ?= as
# lib archiever
AR ?= ar
ARFLAGS ?= rcs

#strip
STRIP ?= strip

# common defines
DEFINES ?= -iquote$(BSEC_DIR)/API -iquote$(BSEC_DIR)/algo/$(ARCH) -iquote$(BSEC_DIR)/examples -lm -lrt -L$(BSEC_DIR)/algo/$(ARCH) -lalgobsec 

# Default release options
ifndef DEBUG

# Release only defines
DEFINES +=

# C compiler flags
CFLAGS ?= -O2 -Wall -g -static

#-Wno-unused-but-set-variable -Wno-unused-variable -static -std=c99 -pedantic

# C++ compiler flags
CXXFLAGS ?= $(CFLAGS)

# Linker flags
LDFLAGS ?= -lpigpio -lm -lrt -lcurl -lpthread -L$(BSEC_DIR)/algo/$(ARCH) -lalgobsec \

# Assembler flags
ASFLAGS ?= 

else # Debug target

# Additional debuf define flags
DEFINES += -D DEBUG

# Override default flags
CFLAGS ?= -O0 -g -Wall
CXXFLAGS ?= $(CFLAGS)
LDFLAGS ?=
ASFLAGS ?=
#YFLAGS ?=
#LFLAGS ?=

# Append additional debug libraries
LIBS += 
DEFINES +=

endif

