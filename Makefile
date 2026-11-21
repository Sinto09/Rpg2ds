# Makefile - RPG 2DS (devkitARM + libctru + citro2d)
# Tous les fichiers .c a la racine du depot sont compiles automatiquement.

.RECIPEPREFIX := >

ifeq ($(strip $(DEVKITARM)),)
$(error DEVKITARM n'est pas defini)
endif
ifeq ($(strip $(DEVKITPRO)),)
$(error DEVKITPRO n'est pas defini)
endif

TARGET   := rpg2ds
CTRULIB  := $(DEVKITPRO)/libctru
PORTLIBS := $(DEVKITPRO)/portlibs/3ds

CC       := $(DEVKITARM)/bin/arm-none-eabi-gcc
TOOL3DSX := $(DEVKITPRO)/tools/bin/3dsxtool

ARCH     := -march=armv6k -mtune=mpcore -mfloat-abi=hard -mtp=soft
CFLAGS   := -g -Wall -O2 -mword-relocations -ffunction-sections $(ARCH) \
            -D__3DS__ -I$(CTRULIB)/include -I$(PORTLIBS)/include
LDFLAGS  := -specs=3dsx.specs -g $(ARCH) -L$(CTRULIB)/lib -L$(PORTLIBS)/lib
LIBS     := -lcitro2d -lcitro3d -lctru -lm

SOURCES  := $(wildcard *.c)
OBJECTS  := $(SOURCES:%.c=build/%.o)

.PHONY: all clean

all: $(TARGET).3dsx

build/%.o: %.c
> @mkdir -p build
> $(CC) $(CFLAGS) -c $< -o $@

$(TARGET).elf: $(OBJECTS)
> $(CC) $(LDFLAGS) $(OBJECTS) $(LIBS) -o $@

$(TARGET).3dsx: $(TARGET).elf
> $(TOOL3DSX) $< $@

clean:
> rm -rf build $(TARGET).elf $(TARGET).3dsx
