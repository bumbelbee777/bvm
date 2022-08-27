posix:
	clang++ -shared *.cpp -o libvm.so -I. -fpic

win32:
	clang++ -shared *.cpp -o libvm.dll -I. -fpic

clean-posix:
	rm -rf libvm.so bvm

clean-win32:
	del libvm.dll bvm.exe

bvm-posix:
	clang++ emulator/bvm.cpp -o bvm -I. -L. -lbvm

bvm-win32:
	clang++ emulator/bvm.cpp -o bvm.exe -I. -L. -lbvm