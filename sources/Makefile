posix:
	clang++ -shared libvm/*.cpp -o libvm.so -Ilibvm/ -fpic

win32:
	clang++ -shared libvm/*.cpp -o libvm.dll -Ilibvm/ -Ilibvm

clean-posix:
	rm -rf libvm.so

clean-win32:
	del libvm.dll

install-posix:
	sudo mkdir /usr/lib/libvm
	sudo cp libvm.so -o /usr/lib/libvm
