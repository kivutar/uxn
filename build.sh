#!/bin/sh -e

echo "Cleaning.."
rm -f ./bin/uxnasm
rm -f ./bin/uxnemu
rm -f ./bin/uxncli
rm -f ./bin/boot.rom

# When clang-format is present

if [ "${1}" = '--format' ]; 
then
	echo "Formatting.."
	clang-format -i src/uxn.h
	clang-format -i src/uxn.c
	clang-format -i src/devices/ppu.h
	clang-format -i src/devices/ppu.c
	clang-format -i src/devices/apu.h
	clang-format -i src/devices/apu.c
	clang-format -i src/uxnasm.c
	clang-format -i src/uxnemu.c
	clang-format -i src/uxncli.c
fi

mkdir -p bin
CFLAGS="-std=c89 -Wall -Wno-unknown-pragmas"
case "$(uname -s 2>/dev/null)" in
MSYS_NT*) # MSYS2 on Windows
	UXNEMU_LDFLAGS="-static $(sdl2-config --cflags --static-libs)"
	;;
Darwin) # macOS
	CFLAGS="${CFLAGS} -Wno-typedef-redefinition"
	UXNEMU_LDFLAGS="/usr/local/lib/libSDL2.a $(sdl2-config --cflags --static-libs | sed -e 's/-lSDL2 //')"
	;;
Linux|*)
	UXNEMU_LDFLAGS="-L/usr/local/lib $(sdl2-config --cflags --libs)"
	;;
esac

if [ "${1}" = '--debug' ]; 
then
	echo "[debug]"
	CFLAGS="${CFLAGS} -DDEBUG -Wpedantic -Wshadow -Wextra -Werror=implicit-int -Werror=incompatible-pointer-types -Werror=int-conversion -Wvla -g -Og -fsanitize=address -fsanitize=undefined"
	CORE='src/uxn.c'
else
	CFLAGS="${CFLAGS} -DNDEBUG -Os -g0 -s"
	CORE='src/uxn-fast.c'
fi

echo "Building.."
cc ${CFLAGS} src/uxnasm.c -o bin/uxnasm
cc ${CFLAGS} ${CORE} src/devices/ppu.c src/devices/apu.c src/uxnemu.c ${UXNEMU_LDFLAGS} -o bin/uxnemu
cc ${CFLAGS} ${CORE} src/uxncli.c -o bin/uxncli

if [ -d "$HOME/bin" ]
then
	echo "Installing in $HOME/bin"
	cp bin/uxnemu bin/uxnasm bin/uxncli $HOME/bin/
fi

# echo "Assembling(uxnasm).."
# ./bin/uxnasm projects/examples/demos/piano.tal bin/piano.rom

echo "Assembling(asma).."
./bin/uxnasm projects/software/asma.tal bin/asma.rom

echo "Assembling(piano).."
echo projects/examples/demos/piano.tal | bin/uxncli bin/asma.rom > bin/piano.rom 2> bin/piano.log

echo "Running.."
./bin/uxnemu bin/piano.rom

echo "Done."
