#ifndef PROFILING_H__
#define PROFILING_H__

#include <memory>
#include <boost/date_time/posix_time/posix_time.hpp>
#include "Identity.h"

namespace i2p
{
namespace data
{	
	// sections
	const char PEER_PROFILE_SECTION_PARTICIPATION[] = "participation";
	const char PEER_PROFILE_SECTION_USAGE[] = "usage";
	const char PEER_PROFILE_SECTION_BAN[] = "ban";
	// params	
	const char PEER_PROFILE_LAST_UPDATE_TIME[] = "lastupdatetime";
	const char PEER_PROFILE_PARTICIPATION_AGREED[] = "agreed";
	const char PEER_PROFILE_PARTICIPATION_DECLINED[] = "declined";
	const char PEER_PROFILE_PARTICIPATION_NON_REPLIED[] = "nonreplied";	
	const char PEER_PROFILE_USAGE_TAKEN[] = "taken";
	const char PEER_PROFILE_USAGE_REJECTED[] = "rejected";
	const char PEER_PRPFILE_BAN_REASON[] = "reason";
  
	const int PEER_PROFILE_EXPIRATION_TIMEOUT = 72; // in hours (3 days)
  
	class RouterProfile
	{
		public:

			RouterProfile (const IdentHash& identHash);
			RouterProfile& operator= (const RouterProfile& ) = default;
			
			void Save ();
			void Load ();

			bool IsBad ();
			
			void TunnelBuildResponse (uint8_t ret);
			void TunnelNonReplied ();

			void BanWithReason(const std::string & reason);
			void Ban();

			void Unban();
		
			bool IsBanned();
			std::string GetBanReason() const { return m_BanReason; }

			uint32_t GetTunnelsAgreed() const { return m_NumTunnelsAgreed; }
			uint32_t GetTunnelsDeclined() const { return m_NumTunnelsDeclined; }
			uint32_t GetTunnelsTimeout() const { return m_NumTunnelsNonReplied; }
		
			uint32_t GetTaken() const { return m_NumTimesTaken; }
			uint32_t GetRejected() const { return m_NumTimesRejected; }

		private:

			boost::posix_time::ptime GetTime () const;
			void UpdateTime ();

			bool IsAlwaysDeclining () const { return !m_NumTunnelsAgreed && m_NumTunnelsDeclined >= 5; };
			bool IsLowPartcipationRate () const;
			bool IsLowReplyRate () const;
			
		private:	

			IdentHash m_IdentHash;
			boost::posix_time::ptime m_LastUpdateTime;
			// participation
			uint32_t m_NumTunnelsAgreed;
			uint32_t m_NumTunnelsDeclined;	
			uint32_t m_NumTunnelsNonReplied;
			// usage
			uint32_t m_NumTimesTaken;
			uint32_t m_NumTimesRejected;
			// ban
			std::string m_BanReason;
	};	

	std::shared_ptr<RouterProfile> GetRouterProfile (const IdentHash& identHash); 
	void InitProfilesStorage ();
	void DeleteObsoleteProfiles ();
}		
}	

#endif
