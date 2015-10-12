#include "Stats.h"

namespace i2p
{
namespace stats
{

EventPumper * g_ev_pumper = nullptr;

void EventPumper::AddEventListener(EventType evtype, EventHandler listener)
{
  std::lock_guard<std::mutex> lock(m_handlers_mtx);
  m_handlers[evtype].push_back(listener);
}

void EventPumper::GotEvent(EventType type, EventData const & d, Timestamp ts)
{
  std::lock_guard<std::mutex> lock(m_handlers_mtx);
  auto search = m_handlers.find(type);
  if (search != m_handlers.end())
  {
    // we have handlers
    for ( auto & handler : search->second )
    {
      // call handler
      // TODO: use another thread?
      handler(d, ts);
    }
  }
}

void Event::Process()
{
  if (pumper != nullptr)
  {
    pumper->GotEvent(this->type, this->data, this->timestamp);
  }
}

void SentI2NP(uint8_t msgtype, i2p::data::IdentHash const & dst)
{
  if(g_ev_pumper)
  {
    EventData dat;
    // type
    dat.push_back(std::to_string(msgtype));
    // src
    dat.push_back("US");
    // dst
    dat.push_back(dst.ToBase64());
    Event * ev = new Event(g_ev_pumper, eEventI2NP, dat);
    g_ev_pumper->Put(ev);
  }
}

void RecvI2NP(uint8_t msgtype, i2p::data::IdentHash const & source)
{
  if(g_ev_pumper)
  {
    EventData dat;
    // type
    dat.push_back(std::to_string(msgtype));
    // src
    dat.push_back(source.ToBase64());
    // dst
    dat.push_back("US"); 
    Event * ev = new Event(g_ev_pumper, eEventI2NP, dat);
    g_ev_pumper->Put(ev);
  }
}
  
}
}
