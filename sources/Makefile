posix:
	clang++ -shared *.cpp -o libvm.so -I. -fpic

win32:
	clang++ -shared *.cpp -o libvm.dll -I.

clean-posix:
	rm -rf libvm.so

clean-win32:
	del libvm.dll