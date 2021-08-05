CXX = g++
CXXFLAGS = -g -O2
LDLIBS = -lpthread

all: main.cpp
	$(CXX) $(CXXFLAGS) -o ssh-agent-bridge-wsl2-helper main.cpp $(LDLIBS)

clean:
	rm -rf ssh-agent-bridge-wsl2-helper