#include <string>
#include <music.hh>

struct PyObject;
class PreSyn;
class Gi2PreSynTable;

// Interface which nrnmusic.so module (which mdj writes) will use

namespace NRNMUSIC {

  class EventOutputPort : public MUSIC::EventOutputPort {
  public:
    EventOutputPort (MUSIC::Setup* s, std::string id)
      : MUSIC::EventOutputPort (s, id) { }
    
    // Connect a gid to a MUSIC global index in this port
    void gid2index (int gid, int gi);
  };

  class EventInputPort : public MUSIC::EventInputPort {
  public:
    EventInputPort (MUSIC::Setup* s, std::string id);
    
    // Connect a MUSIC global index to a gid in this port
    PyObject* index2target (int gi, PyObject* target);
  private:
    Gi2PreSynTable* gi_table;
  };

  EventOutputPort* publishEventOutput (std::string identifier);

  EventInputPort* publishEventInput (std::string identifier);

}
