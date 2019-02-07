CXX=llvm-g++ # llvm usually gives better error messages than gnu g++
# TinyXML2 and TinyXML2-ex are used for parsing XML; they are included as submodules in the repository
CXXFLAGS=-Ilib/tinyxml2 -Ilib/tinyxml2-ex -g3

all:	hpf

clean:
	rm -f hpf
