#include "NTCP2.h"
#include "NTCP2Session.h"
#include "RouterInfo.h"
#include "RouterContext.h"
#include "NetDb.hpp"
#include "Transports.h"
#include "Config.h"
#include "FS.h"

struct Main_Wrapper
{
  Main_Wrapper()
  {
    i2p::log::Logger().SetLogLevel("debug");
    i2p::log::Logger().Start();
    i2p::transport::transports.Start(false, false, true);
    i2p::data::netdb.Start();
  }
  ~Main_Wrapper()
  {
    i2p::data::netdb.Stop();
    i2p::transport::transports.Stop();
    i2p::log::Logger().Stop();
  }
};

int main(int argc, char * argv[])
{
  std::string rifile;
  if (argc == 2)
  {
    rifile = argv[1];
  }
  else
    return 1;

  
  i2p::crypto::InitCrypto(false);
  char datadir[] = "ntcp2_test";
  char datadiropt[] = "--datadir=ntcp2_test";
  char reseedopt[] =  "--reseed.disabled=true";
  i2p::config::Init();
  i2p::fs::DetectDataDir(datadir);
  i2p::fs::Init();
  char * args[] = {argv[0], datadiropt, reseedopt};
  i2p::config::ParseCmdline(3, args, true);
  i2p::config::Finalize();
  i2p::context.Init();
  std::shared_ptr<const i2p::data::RouterInfo> remoteRI;
  {
    Main_Wrapper m;
    {
      std::ifstream f(rifile);
      if(!f.is_open())
        return 1;
      char buf[i2p::data::MAX_RI_BUFFER_SIZE];
      f.seekg(0, std::ios::end);
      size_t sz = f.tellg();
      if(sz <= sizeof(buf))
      {
        f.seekg(0, std::ios::beg);
        f.read(buf, sz);
        remoteRI = std::make_shared<const i2p::data::RouterInfo>((uint8_t*)buf, sz);
        i2p::data::netdb.AddRouterInfo(remoteRI->GetBuffer(), remoteRI->GetBufferLen());
        i2p::data::netdb.Flush();
      }
      else
      {
        return 1;
      }
    }
    if(!remoteRI) return 1;
    // make it connect right away
    i2p::data::netdb.ReseedFromFloodfill(*remoteRI);
    auto ident = remoteRI->GetIdentHash();
    std::this_thread::sleep_for(std::chrono::seconds(1));
    auto session = i2p::transport::transports.GetNTCP2Server()->FindSession(ident);
    if(session && session->IsEstablished())
    {
      LogPrint(eLogInfo, "TEST: outbound NTCP2 session established with ", ident.ToBase64());
    }
    else
    {
      LogPrint(eLogError, "TEST: outbound NTCP2 session failed to established with ", ident.ToBase64());
    }
  }
  return 0;
}
