lib-posix:
	make -C sources/ posix

lib-win32:
	make -C sources/ win32

bvm-posix:
	make -C sources/emulator posix

bvm-win32:
	make -C sources/emulator win32

bas-posix:
	make -C sources/assembler posix

bas-win32:
	make -C sources/assembler win32