
using namespace imajuscule;
using namespace imajuscule::sensor;


Audio * Audio::gInstance = nullptr;
Audio * Audio::getInstance()
{
    return Globals::ptr<Audio>(gInstance);
}

void Audio::Init(OutInitPolicy p) {
    if(auto i = Audio::getInstance()) {
        i->doInit(p);
    }
}

void Audio::TearDown() {
    if(auto i = Audio::getInstance()) {
        i->doTearDown();
    }
}

void Audio::doInit(OutInitPolicy p) {
    imajuscule::audio::overridePortaudioMinLatencyMillis(4); // TODO adapt to minLatency?

#ifndef NO_AUDIO_IN
    audioIn.Init();
#endif
    if(p == OutInitPolicy::FORCE) {
      audioOut.Init(audio::AudioOut::AudioCtxt::minLazyLatency);
    }
}

void Audio::doTearDown() {
    audioOut.TearDown();
#ifndef NO_AUDIO_IN
    audioIn.TearDown();
#endif
}
