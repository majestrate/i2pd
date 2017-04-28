#ifndef MATCHED_DESTINATION_H_
#define MATCHED_DESTINATION_H_
#include "Destination.h"
#include <string>

namespace i2p
{
namespace client
{
	/**
		 client tunnel that uses same OBEP as IBGW of each remote lease for a remote destination
	 */
	class MatchedTunnelDestination : public ClientDestination, public i2p::tunnel::ITunnelPeerSelector
	{
	public:
		MatchedTunnelDestination(const i2p::data::PrivateKeys& keys, const std::string & remoteName, const std::map<std::string, std::string> * params = nullptr);
		bool Start();
		bool Stop();

		bool SelectPeers(i2p::tunnel::Path & peers, int hops, bool inbound);
		bool OnBuildResult(const i2p::tunnel::Path & peers, bool inbound, i2p::tunnel::TunnelBuildResult result);
		virtual void CreateStream (StreamRequestComplete streamRequestComplete, const i2p::data::IdentHash& dest, int port = 0);
    virtual std::shared_ptr<i2p::tunnel::OutboundTunnel> GetNewOutboundTunnelForLease(const std::shared_ptr<i2p::tunnel::OutboundTunnel> & excluding, const i2p::data::IdentHash & remoteLease);
	private:
		void ResolveCurrentLeaseSet();
		void HandleFoundCurrentLeaseSet(std::shared_ptr<const i2p::data::LeaseSet> ls);
		bool OutboundTunnelsAreReady() { return m_HasPath; };

	private:
		std::string m_RemoteName;
		i2p::data::IdentHash m_RemoteIdent;
		std::shared_ptr<const i2p::data::LeaseSet> m_RemoteLeaseSet;
		std::shared_ptr<boost::asio::deadline_timer> m_ResolveTimer;
		bool m_HasPath;
	};
}
}

#endif
