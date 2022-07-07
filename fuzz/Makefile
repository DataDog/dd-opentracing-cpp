.PHONY: fuzz-glob
fuzz-glob: glob
	./glob -jobs=$(shell nproc) -workers=$(shell nproc)

GLOB_DEPS = glob.cpp \
	../src/glob.h \
	../src/glob.cpp \
	../deps/include/opentracing/string_view.h \
	../deps/include/opentracing/version.h

glob: $(GLOB_DEPS)
	clang++ -I ../deps/include -g -fsanitize=address,undefined,fuzzer -Wall -Wextra -pedantic -Werror -o $@ glob.cpp ../src/glob.cpp

glob-manual: glob-manual.cpp $(GLOB_DEPS)
	clang++ -I ../deps/include -g -fsanitize=address,undefined -Wall -Wextra -pedantic -Werror -o $@ glob-manual.cpp glob.cpp ../src/glob.cpp
	
