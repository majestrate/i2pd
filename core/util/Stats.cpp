#include "Stats.h"

namespace i2p
{
namespace stats
{

EventPumper * g_ev_pumper = nullptr;

HandlerID EventPumper::AddEventListener(EventType evtype, EventHandler listener)
{
  std::lock_guard<std::mutex> lock(m_handlers_mtx);
  if(m_NextID == 0) m_NextID++;
  auto id = m_NextID;
  m_handlers[evtype].push_back({listener, id});
  m_NextID++;
  return id;
}

void EventPumper::RemoveEventListener(HandlerID id)
{
  std::lock_guard<std::mutex> lock(m_handlers_mtx);

  auto itr = m_handlers.begin();
  while(itr != m_handlers.end())
  {
    auto hitr = itr->second.begin();
    while(hitr != itr->second.end())
    {
      if (hitr->second == id)
      {
        itr->second.erase(hitr);
      }
    }
  }
}

void EventPumper::GotEvent(EventType type, EventData const & d, Timestamp ts)
{
  std::lock_guard<std::mutex> lock(m_handlers_mtx);
  auto search = m_handlers.find(type);
  if (search != m_handlers.end())
  {
    // we have handlers
    for ( auto & item : search->second )
    {
      // call handler
      // TODO: use another thread?
      item.first(d, ts);
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
