CXXFLAGS += -Wall -Wextra -pedantic -O0 -g -std=c++11 -D_GLIBCXX_USE_NANOSLEEP=1 -I../libi2pd/ -pthread -mavx -mavx2

TESTS = test-gost test-gost-sig test-base-64 test-poly1305 test-chacha20 test-siphash test-x25519-scalarmult
DEPS = ../libi2pd/ref10/ref10.cpp ../libi2pd/sandy2x/sandy2x_asm.S ../libi2pd/sandy2x/sandy2x.cpp ../libi2pd/sandy2x/fe_frombytes.cpp ../libi2pd/sandy2x/invert.cpp ../libi2pd/Ed25519.cpp

all: clean test

test: $(TESTS) run

test-http-%: ../libi2pd/HTTP.cpp test-http-%.cpp
	$(CXX) $(CXXFLAGS) $(NEEDED_CXXFLAGS) $(INCFLAGS) -o $@ $^

test-base-%: ../libi2pd/Base.cpp test-base-%.cpp
	$(CXX) $(CXXFLAGS) $(NEEDED_CXXFLAGS) $(INCFLAGS) -o $@ $^

test-gost: $(DEPS) ../libi2pd/Log.cpp ../libi2pd/CPU.cpp ../libi2pd/Gost.cpp ../libi2pd/I2PEndian.cpp test-gost.cpp
	$(CXX) $(CXXFLAGS) $(NEEDED_CXXFLAGS) $(INCFLAGS) -o $@ $^ -lcrypto

test-gost-sig: $(DEPS) ../libi2pd/CPU.cpp ../libi2pd/Gost.cpp ../libi2pd/I2PEndian.cpp ../libi2pd/Signature.cpp ../libi2pd/Crypto.cpp ../libi2pd/Log.cpp test-gost-sig.cpp
	$(CXX) $(CXXFLAGS) $(NEEDED_CXXFLAGS) $(INCFLAGS) -o $@ $^ -lcrypto -lssl -lboost_system

test-poly1305: ../libi2pd/Log.cpp ../libi2pd/CPU.cpp ../libi2pd/Poly1305.cpp test-Poly1305.cpp
	$(CXX) $(CXXFLAGS) $(NEEDED_CXXFLAGS) $(INCFLAGS) -o $@ $^

test-chacha20: ../libi2pd/Log.cpp ../libi2pd/CPU.cpp ../libi2pd/ChaCha20.cpp test-ChaCha20.cpp
	$(CXX) $(CXXFLAGS) $(NEEDED_CXXFLAGS) $(INCFLAGS) -o $@ $^

test-siphash: test-siphash.cpp
	$(CXX) $(CXXFLAGS) $(NEEDED_CXXFLAGS) $(INCFLAGS) -o $@ $^

test-x25519-scalarmult: $(DEPS) ../libi2pd/Log.cpp ../libi2pd/CPU.cpp ../libi2pd/Crypto.cpp ../libi2pd/I2PEndian.cpp test-x25519-scalarmult.cpp
	$(CXX) $(CXXFLAGS) $(NEEDED_CXXFLAGS) $(INCFLAGS) -o $@ $^ -lcrypto -lssl -lboost_system

run: $(TESTS)
	@for TEST in $(TESTS); do echo "$$TEST" ; ./$$TEST || echo "$$TEST failed" ; done

clean:
	rm -f $(TESTS)
