CXXFLAGS += -Wall -Wextra -pedantic -O0 -g -std=c++11 -D_GLIBCXX_USE_NANOSLEEP=1 -I../libi2pd/ -pthread -mavx2 -mavx

TESTS = test-gost test-gost-sig test-base-64 test-poly1305 test-chacha20 test-siphash test-x25519-scalarmult test-ntcp2
DEP = ../libi2pd.a

all: clean test

test: $(TESTS) run

test-http-%: test-http-%.cpp $(DEP) 
	$(CXX) $(CXXFLAGS) $(NEEDED_CXXFLAGS) $(INCFLAGS) -o $@ $^

test-base-%: test-base-%.cpp $(DEP) 
	$(CXX) $(CXXFLAGS) $(NEEDED_CXXFLAGS) $(INCFLAGS) -o $@ $^

test-gost: test-gost.cpp $(DEP)
	$(CXX) $(CXXFLAGS) $(NEEDED_CXXFLAGS) $(INCFLAGS) -o $@ $^ -lcrypto -lssl -lboost_system

test-gost-sig: test-gost-sig.cpp $(DEP) 
	$(CXX) $(CXXFLAGS) $(NEEDED_CXXFLAGS) $(INCFLAGS) -o $@ $^ -lcrypto -lssl -lboost_system

test-poly1305: test-Poly1305.cpp $(DEP)
	$(CXX) $(CXXFLAGS) $(NEEDED_CXXFLAGS) $(INCFLAGS) -o $@ $^

test-chacha20: test-ChaCha20.cpp $(DEP) 
	$(CXX) $(CXXFLAGS) $(NEEDED_CXXFLAGS) $(INCFLAGS) -o $@ $^

test-siphash: test-siphash.cpp
	$(CXX) $(CXXFLAGS) $(NEEDED_CXXFLAGS) $(INCFLAGS) -o $@ $^

test-x25519-scalarmult: test-x25519-scalarmult.cpp $(DEP)
	$(CXX) $(CXXFLAGS) $(NEEDED_CXXFLAGS) $(INCFLAGS) -o $@ $^ -lcrypto -lssl -lboost_system

test-ntcp2: test-ntcp2.cpp $(DEP)
	$(CXX) $(CXXFLAGS) $(NEEDED_CXXFLAGS) $(INCFLAGS) -o $@ $^ -lcrypto -lssl -lboost_system -lboost_program_options -lboost_filesystem -lboost_date_time -lz

run: $(TESTS)
	@for TEST in $(TESTS); do echo "$$TEST" ; ./$$TEST || echo "$$TEST failed" ; done

clean:
	rm -f $(TESTS)
