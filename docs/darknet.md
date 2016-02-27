== I2P darknet mode ==

I2P darknet mode is a mode of operation that goes over a private network like a cjdns mesh network

Current status: testing phase

How to build i2pd for darknet mode:

    git clone https://github.com/majestrate/i2pd -b darknet i2pd_darknet
    cd i2pd_darknet/build
    cmake -DCMAKE_CXX_FLAGS="-DI2PD_NET_ID=3" ..
    make 
