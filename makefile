CC=clang++
APP=toy
LLVM_CONFIG=--cxxflags --ldflags --system-libs --libs core orcjit native

SOURCE=toy.cpp

all: ${SOURCE}
	clang++ -g -O3 ${SOURCE} `llvm-config ${LLVM_CONFIG}` -o ${APP} -v

clean:
	@rm -rf *.o ${APP}
