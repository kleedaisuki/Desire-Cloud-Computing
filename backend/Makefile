DEBUG = true

compiler = g++

ifeq ($(DEBUG), true)
optimization = -Og -g
else
optimization = -O3
endif

standard = -std=c++20

source_code = main.cpp write-log.cpp compile-thread.cpp global-constant.cpp \
network/class.Accepter.cpp network/class.Buffer.cpp network/class.Channel.cpp network/class.EventLoop.cpp \
network/class.TcpConnection.cpp network/class.TcpServer.cpp

executable = cpl-back

all : $(executable)
ifeq ($(DEBUG), true)
	mv $(executable) ../test-dir/
endif
	make clean

$(executable) : $(source_code:.cpp=.o)
	$(compiler) $(optimization) $(standard) *.o -o $(executable)

%.o : %.cpp
	$(compiler) $(standard) -c $(optimization) $<

network/%.o: network/%.cpp
	$(compiler) $(standard) -c $(optimization) $<

clean :
	rm *.o
