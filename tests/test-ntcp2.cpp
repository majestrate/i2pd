#include "NTCP2.h"
#include "NTCP2Session.h"
#include "RouterInfo.h"
#include "Log.h"

int main(int argc, char * argv[])
{
  std::string remoteip;
  uint16_t remoteport;
  std::string rifile;
  if (argc == 4)
  {
    remoteip = argv[1];
    remoteport = std::atoi(argv[2]);
    rifile = argv[3];
  }
  else
    return 1;

  i2p::log::Logger().SetLogLevel("debug");
  i2p::log::Logger().Start();
  i2p::crypto::InitCrypto(false);

  auto remoteRI = std::make_shared<const i2p::data::RouterInfo>(rifile);
  auto ident = remoteRI->GetIdentHash();
  i2p::transport::NTCP2Server ntcp2;
  ntcp2.Start();
  LogPrint(eLogInfo, "TEST: outbound NTCP2 connection to ", ident.ToBase64());
  auto conn = std::make_shared<i2p::transport::NTCP2Session>(ntcp2, remoteRI);
  ntcp2.Connect(i2p::transport::NTCP2Server::IP_t::from_string(remoteip), remoteport, conn);
  std::this_thread::sleep_for(std::chrono::seconds(1));
  auto session = ntcp2.FindSession(ident);
  if(session && session->IsEstablished())
  {
    LogPrint(eLogInfo, "TEST: outbound NTCP2 session established with ", ident.ToBase64());
  }
  else
  {
    LogPrint(eLogError, "TEST: outbound NTCP2 session failed to established with ", ident.ToBase64());
  }
  ntcp2.Stop();
  i2p::log::Logger().Stop();
  return 0;
}
