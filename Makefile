SHARED := -shared
TARGET := uxn_libretro.so

ifeq ($(shell uname -s),) # win
	SHARED := -shared
	TARGET := uxn_libretro.dll
else ifneq ($(findstring MINGW,$(shell uname -s)),) # win
	SHARED := -shared
	TARGET := uxn_libretro.dll
else ifneq ($(findstring Darwin,$(shell uname -s)),) # osx
	SHARED := -dynamiclib
	TARGET := uxn_libretro.dylib
endif

CFLAGS += -O3 -fPIC -flto

OBJ = src/devices/ppu.o src/devices/apu.o src/uxn.o src/uxnemu.o

%.o: %.c
	$(CC) -c -o $@ $< $(CFLAGS)

$(TARGET): $(OBJ)
	$(CC) $(SHARED) -o $@ $^ $(CFLAGS)

clean:
	rm $(OBJ) $(TARGET)
