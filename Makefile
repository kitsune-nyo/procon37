CXX = g++
CPPFLAGS ?=
CXXFLAGS ?= -std=c++17 -O2
LDFLAGS ?=
LDLIBS = -lcurl

TARGET = output/local/hexudon_main
SOURCES = hexudon_main.cpp state.cpp map.cpp spot.cpp agent.cpp json.hpp

.PHONY: all clean visualizer

all: $(TARGET)

$(TARGET): $(SOURCES)
	mkdir -p $(@D)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) hexudon_main.cpp $(LDFLAGS) $(LDLIBS) -o $@

clean:
	rm -f $(TARGET)

visualizer:
	python3 visualizer.py
