#include "NTCP2.h"
#include "NTCP2Session.h"
#include "RouterInfo.h"
#include "RouterContext.h"
#include "NetDb.hpp"
#include "Transports.h"
#include "Config.h"
#include "FS.h"

int main(int argc, char * argv[])
{
  std::string rifile;
  if (argc == 2)
  {
    rifile = argv[1];
  }
  else
    return 1;

  i2p::log::Logger().SetLogLevel("debug");
  i2p::log::Logger().Start();
  i2p::crypto::InitCrypto(false);

  i2p::config::Init();
  i2p::fs::DetectDataDir("ntcp2_test");
  i2p::fs::Init();
  char * args[] = {argv[0], "--reseed.disabled=true"};
  i2p::config::ParseCmdline(2, args, true);
  i2p::config::ParseConfig("");
  i2p::config::Finalize();

  i2p::context.Init();
  i2p::transport::transports.Start(false, false, true);
  i2p::data::netdb.Start();
  i2p::data::RouterInfo remoteRI(rifile);
  i2p::data::netdb.AddRouterInfo(remoteRI.GetBuffer(), remoteRI.GetBufferLen());
  
  // make it connect right away
  i2p::data::netdb.ReseedFromFloodfill(remoteRI);
  auto ident = remoteRI.GetIdentHash();
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
  i2p::transport::transports.Stop();
  i2p::data::netdb.Stop();
  i2p::log::Logger().Stop();
  return 0;
}
