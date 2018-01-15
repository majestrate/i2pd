#ifndef NET_H__
#define NET_H__

#include <sys/types.h>
#include <sys/socket.h>
#include <functional>
#include <memory>
#include <vector>

namespace i2p
{
  namespace net
  {

    /** network error codes */
    enum Error
    {
      eSuccess,
      eTimeout,
      eReset,
      eHostNotFound,
      eNoRoute,
      eUnreachable,
      eWriteError,
      eReadError,
      eOutOfFiles
    };

    typedef std::function<void(Error, uint8_t *, size_t)> IOHandler;

    typedef IOHandler WriteHandler;
    typedef IOHandler ReadHandler;

    /** base network endpoint type */
    struct Endpoint
    {
      virtual ~Endpoint() {};
      virtual std::string to_string() const = 0;
      virtual struct sockaddr * SAddr() const = 0;
      virtual int AF() const = 0;
      virtual int Proto() const = 0;
      virtual void Port(uint16_t port);
      virtual uint16_t Port() const = 0;
    };
    
    struct StreamEndpoint : public Endpoint
    {
    };
    typedef std::shared_ptr<StreamEndpoint> StreamEndpoint_ptr;
    
    struct PacketEndpoint : public Endpoint
    {
    };

    
    typedef std::shared_ptr<PacketEndpoint> PacketEndpoint_ptr;
    
    /** stream socket */
    struct StreamConn
    {
      virtual ~StreamConn() {};
      virtual void Close() = 0;
      virtual void AsyncWrite(uint8_t * buff, size_t sz, WriteHandler handler) = 0;
      virtual void AsyncRead(uint8_t * buff, size_t sz, ReadHandler handler) = 0;
      virtual const StreamEndpoint * LocalAddr() const = 0;
      virtual const StreamEndpoint * RemoteAddr() const = 0;
    };

    typedef std::shared_ptr<StreamConn> StreamConn_ptr;
    
    typedef std::function<void(Error, StreamConn_ptr)> ConnectHandler;

    /** 
        get local stream endpoint via interface with name ifname on port 
        returns nullptr if interface does not exist
     */
    StreamEndpoint_ptr IfAddr(const std::string & ifname, uint16_t port=0);
    
    /** 
        get stream endpoint for localhost on port 
        returns nullptr if we have no loopback interface
    */
    StreamEndpoint_ptr Localhost(uint16_t port=0);

    /** stream acceptor */
    struct Acceptor
    {
      virtual ~Acceptor() {};
      virtual void Close() = 0;
      virtual const StreamEndpoint * LocalAddr() const = 0;
      /** override me */
      virtual void HandleAccept(StreamConnPtr conn) = 0;
    };
    
    /** udp socket */
    struct PacketConn
    {
      virtual ~PacketConn() {};
      virtual void Close() = 0;
      virtual void SendTo(const PacketEndpoint & remote, const uint8_t * buff, size_t sz) = 0;
      /** override me */
      virtual void HandleRecvFrom(const PacketEndpoint & remote, uint8_t * buff, size_t sz) {};
    };

    typedef std::shared_ptr<PacketConn> PacketConn_ptr;

    typedef std::function<void(Error, std::vector<StreamEndpoint>)> StreamEndpointResolveHandler;
    typedef std::function<void(Error, std::vector<PacketEndpoint>)> PacketEndpointResolveHandler;

    /** name resolver */
    struct Resolver
    {
      virtual ~Resolver()  {};
      virtual void AsyncResolve(const std::string & name, StreamEndpointResolveHandler h) = 0;
      virtual void AsyncResolve(const std::string & name, PacketEndpointResolveHandler h) = 0;
    };

    typedef std::shared_ptr<Resolver> Resolver_ptr;

    /** network context */
    struct NetContext
    {
      typedef std::function<void(Error, PacketConn_ptr)> PacketConnBindHandler;
      typedef std::function<void(Error, Acceptor_ptr)> StreamConnBindHandler;
      
      virtual ~NetContext() {};
      virtual void Start() = 0;
      virtual void Stop() = 0;
      virtual Resolver_ptr Resolver() const = 0;
      virtual void BindPacket(const PacketEndpoint & ep, PacketConnBindHandler h) = 0;
      virtual void BindStream(const StreamEndpoint & ep, StreamConnBindHandler h) = 0;
      virtual void ConnectStream(const StreamEndpoint & ep, ConnectHandler h) = 0;
    };

    typedef std::shared_ptr<NetContext> NetContext_ptr;

    /** create a new net context on network interface with ifname or emtpy string for any/all network interface 
        used for ntcp
        @returns nullptr if the network interface provided does not exist
     */
    NetContext_ptr NTCPNetContext(const std::string & ifname="");
    
    /**
       create a new net context for unix sockets only
       @returns nullptr if unix domain sockets are not supported
     */
    NetContext_ptr UnixNetContext();
    
  }
}

#endif
