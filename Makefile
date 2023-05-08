
CXXFLAGS += -g -lpthread -lmysqlclient
CXX ?= g++

srcs = main.cpp server.cpp http.cpp tools.cpp timer.cpp sqlconn_pool.cpp
defs = $(srcs:%.cpp=defs/%.d)
objs = $(srcs:%.cpp=build/%.o)


all: server

include $(defs)

build/%.o: %.cpp
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) -c $< -o $@

defs/%.d: %.cpp
	@mkdir -p $(@D); \
	set -e; rm -f $@; \
	$(CXX) -MM $< > $@$$$$; \
	sed 's,\(.*\)\.o[ :]*,build/\1.o $@ : ,g' < $@$$$$ > $@; \
	rm -rf $@$$$$


server: $(objs)
	$(CXX) $(CXXFLAGS) $^ -o $@ 

.PHONY: clean
clean:
	@rm -rf build
	@rm -rf defs
	@rm server
