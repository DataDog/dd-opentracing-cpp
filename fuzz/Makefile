glob: glob.cpp \
	../src/glob.h \
	../src/glob.cpp \
	../deps/include/opentracing/string_view.h \
	../deps/include/opentracing/version.h
	clang++ -I ../deps/include -g -fsanitize=address,fuzzer -Wall -Wextra -pedantic -Werror -o $@ glob.cpp ../src/glob.cpp
