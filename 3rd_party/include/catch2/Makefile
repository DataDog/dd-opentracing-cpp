.PHONY: all
all: catch.hpp LICENSE.txt

catch.hpp:
	wget --quiet -O$@ https://github.com/catchorg/Catch2/releases/download/v2.13.6/catch.hpp
	
LICENSE.txt: catch.hpp
	wget --quiet -O$@ https://raw.githubusercontent.com/catchorg/Catch2/devel/LICENSE.txt
