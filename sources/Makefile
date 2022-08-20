all:
	clang++ -shared *.cpp -o libvm.so -I. -fpic

test:
	clang++ Main.cpp -o bvm -I. -fpic

clean:
	rm -rf *.so bvm
