CXX=llvm-g++ # llvm usually gives better error messages than gnu g++
# TinyXML2 and TinyXML2-ex are used for parsing XML; they are included as submodules in the repository
CXXFLAGS=-g3 -std=c++14 -Ilib/tinyxml2/install-dir/include -Ilib/tinyxml2-ex
LDLIBS=lib/tinyxml2/install-dir/lib/libtinyxml2.a

all:	hpf

clean:
	rm -f hpf
