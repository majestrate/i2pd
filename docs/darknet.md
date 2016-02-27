== I2P darknet mode ==

I2P darknet mode is a mode of operation that goes over a private network like a cjdns mesh network

Current status: testing phase

How to build i2pd for darknet mode:

    git clone https://github.com/majestrate/i2pd -b darknet i2pd_darknet
    mkdir build
    cd build
    cmake ../i2pd_darknet/build
    make 
