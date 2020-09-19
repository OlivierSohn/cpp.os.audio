
using namespace imajuscule;
using namespace imajuscule::sensor;


Audio * Audio::gInstance = nullptr;
Audio * Audio::getInstance()
{
    return Globals::ptr<Audio>(gInstance);
}

[[nodiscard]] bool Audio::Init(OutInitPolicy p) {
  if(auto i = Audio::getInstance()) {
    return i->doInit(p);
  }
  return false;
}

void Audio::TearDown() {
    if(auto i = Audio::getInstance()) {
        i->doTearDown();
    }
}

bool Audio::doInit(OutInitPolicy p) {
  imajuscule::audio::overridePortaudioMinLatencyMillis(4); // TODO adapt to minLatency?
  
  bool res = true;
#ifndef NO_AUDIO_IN
  res = audioIn.Init() && res;
#endif
  if(p == OutInitPolicy::FORCE) {
    res = audioOut.Init(audio::AudioOut::AudioCtxt::minLazyLatency) && res;
  }
  return res;
}

void Audio::doTearDown() {
    audioOut.TearDown();
#ifndef NO_AUDIO_IN
    audioIn.TearDown();
#endif
}
