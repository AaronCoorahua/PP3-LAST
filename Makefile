CXX = g++
CXXFLAGS = -std=c++17 -Wall -Wextra -g
LDFLAGS = -lpthread

SOURCES = main.cpp datagram.cpp unreliableTransport.cpp timerC.cpp
OBJECTS = $(SOURCES:.cpp=.o)
EXECUTABLE = rft-client

all: $(EXECUTABLE)

$(EXECUTABLE): $(OBJECTS)
	$(CXX) $(CXXFLAGS) -o $@ $(OBJECTS) $(LDFLAGS)

%.o: %.cpp logging.h
	$(CXX) $(CXXFLAGS) -c $< -o $@

clean:
	rm -f $(OBJECTS) $(EXECUTABLE)

run: $(EXECUTABLE)
	./$(EXECUTABLE) -h isengard.mines.edu -p 12345 -f input.dat -d 3
