DEBUG = true

compiler = g++

ifeq ($(DEBUG), true)
optimization = -Og -g
else
optimization = -O3
endif

standard = -std=c++20

source_code = main.cpp write-log.cpp global-constant.cpp network/class.ClientSocket.cpp network/class.ConnectionManager.cpp \
network/class.MessageHandler.cpp network/class.Receiver.cpp network/class.Sender.cpp
executable = cpl-front

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