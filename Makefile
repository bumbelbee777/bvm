lib-posix:
	make -C sources/ posix

lib-win32:
	make -C sources/ win32

bvm-posix:
	make -C sources/bvm posix

bvm-win32:
	make -C sources/bvm win32

bas-posix:
	make -C sources/assembler posix

bas-win32:
	make -C sources/assembler win32

all-win32: bas-win32 bvm-win32 lib-win32

all-posix: bas-posix bvm-posix lib-win32