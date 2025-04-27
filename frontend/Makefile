DEBUG = true

compiler = g++
ifeq ($(DEBUG), true)
optimization = -Og -g
else
optimization = -O3
endif

source_code = main.cpp write-log.cpp global-constant.cpp
executable = cpl-front

all : $(executable)
ifeq ($(DEBUG), true)
	mv $(executable) ../test-dir/
endif
	make clean

$(executable) : $(source_code:.cpp=.o)
	$(compiler) $(optimization) $(source_code:.cpp=.o) -o $(executable)

%.o : %.cpp
	$(compiler) -c $(optimization) $<

clean :
	rm *.o