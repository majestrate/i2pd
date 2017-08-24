#include "SAM_internal.h"
#include "Log.h"

namespace i2p
{
	namespace client
	{
		namespace sam
		{
			Bridge::Bridge(const std::string & addr, uint16_t port) :
				m_Impl(new BridgeImpl{addr, port}) {}

			Bridge::~Bridge() { delete m_Impl; }

			void Bridge::Start()
			{
				m_Impl->Start();
			}

			void Bridge::Stop()
			{
				m_Impl->Stop();
			}

			SessionInfo_ptr Bridge::FindSession(const std::string & id) const
			{
				return m_Impl->FindSession(id);
			}

			std::list<SessionInfo_ptr> Bridge::ListSessions() const
			{
				return m_Impl->ListSessions();
			}

			/** all code after this line is the private implementation */

			BridgeImpl::BridgeImpl(const std::string & addr, uint16_t port) :
				m_Endpoint(boost::asio::ip::address::from_string(addr), port)
			{

			}

			BridgeImpl::~BridgeImpl()
			{
				Stop();
			}

			void BridgeImpl::Start()
			{
				if(m_Thread) return;
				try
				{
					m_Acceptor = std::make_unique<tcp_acceptor_t>(m_Service);
					m_Acceptor->bind(m_Endpoint);
				}
				catch (std::exception & ex)
				{
					LogPrint(eLogError, "SAM: failed to bind to ", m_Endpoint, " ", ex.what());
					return;
				}
				m_Running = true;
				m_Thread = new std::thread(std::bind(&BridgeImpl::Run, this));
			}

			void BridgeImpl::Stop()
			{
				m_Running = false;

				if(m_Acceptor && m_Acceptor->is_open())
				{
					m_Acceptor->close();
					m_Acceptor.reset(nullptr);
				}
				m_Service.stop();
				if(m_Thread)
				{
					delete m_Thread;
					m_Thread = nullptr;
				}
			}

			void BridgeImpl::Run()
			{
				while(m_Running)
				{
					try
					{
						m_Service.run();
					}
					catch(std::exception & ex)
					{
						LogPrint(eLogError, "SAMBridgeImpl::Run() exception: ", ex.what());
					}
				}
			}

			SessionInfo_ptr BridgeImpl::FindSession(const std::string & id) const
			{
				// TODO : implement this
				return nullptr;
			}

			std::list<SessionInfo_ptr> BridgeImpl::ListSessions() const
			{
				// TODO : implement this
				return {};
			}

		}
	}
}
