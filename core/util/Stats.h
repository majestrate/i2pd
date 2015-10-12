#ifndef STATS_H__
#define STATS_H__

#include <string>
#include <map>
#include <functional>
#include <memory>
#include "Queue.h"
#include "I2NPProtocol.h"

//
// Stats.h -- i2p router fancy statistics and events manager
//
// provides hooks for sending realtime statistics and I2NP event notifications for UI purposes
//
namespace i2p
{
namespace stats
{
// types of events that we can track
enum EventType
{
  // empty event, is ignored
  eEventNull = 0,
  // we got an i2np message
  eEventI2NP,
  // we connected to someone or they connected to us
  eEventTransport,
  // sent every $interval to the status of the router, includes stats like tunnel build success, total bandwith, etc
  eEventStatus,
  eNumEventTypes
};

// to be converted to json array for websockets
typedef std::vector<std::string> EventData;


#if (__GNUC__ == 4) && (__GNUC_MINOR__ <= 6) && !defined(__clang__)
	typedef	std::chrono::monotonic_clock Clock;
#else
	typedef	std::chrono::steady_clock Clock;
#endif

typedef Clock::time_point Timestamp;


typedef std::function<void(EventData, Timestamp)> EventHandler;

struct Event;

class EventPumper : public i2p::util::MsgQueue<Event>
{
public:
  // register an event handler for this even type
  void AddEventListener(EventType evtype, EventHandler listener);
  // we got an event
  void GotEvent(EventType type, EventData const & d, Timestamp ts);
private:
  typedef std::vector<EventHandler> HandlerContainer;
  std::map<EventType, HandlerContainer> m_handlers;
  std::mutex m_handlers_mtx;
};

struct Event
{
  
  EventPumper * pumper;
  EventType type;
  EventData data;
  Timestamp timestamp;
  Event(EventPumper * evp = nullptr, EventType evt = eEventNull, EventData dat = {}) : pumper(evp), type(evt), data(dat), timestamp(Clock::now()) {};

  void Process();
};
  
void RecvI2NP(uint8_t msgtype, i2p::data::IdentHash const & source);
void SentI2NP(uint8_t msgtype, i2p::data::IdentHash const & dest);

extern EventPumper * g_ev_pumper;

inline void StartEvents()
{
  if (!g_ev_pumper)
  {
    g_ev_pumper = new EventPumper();
  }
}

inline void StopEvents()
{
  if(g_ev_pumper)
  {
    auto ev = g_ev_pumper;
    g_ev_pumper = nullptr;
    ev->Stop();
    delete ev;
  }
}

inline void RegisterEventListener(EventType type, EventHandler handler)
{
  StartEvents();
  g_ev_pumper->AddEventListener(type, handler);
}

}
}
  
#endif
