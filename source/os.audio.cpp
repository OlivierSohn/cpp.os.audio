
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

#if TARGET_OS_IOS
# ifndef NO_AUDIO_IN
int initAudioStreams(AudioUnit & audioUnit, imajuscule::sensor::paTestData & chans, AURenderCallback cb, int nOuts, AudioStreamBasicDescription & streamDescription) {
    return audio::initAudioStreams(audio::Features::InAndOut, audioUnit, &chans, cb, nOuts, streamDescription);
}
# endif
#endif

void Audio::doInit(OutInitPolicy p) {
    imajuscule::audio::setPortaudioLatencyMillis(4); // TODO adapt to minLatency?

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
