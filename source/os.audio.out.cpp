
using namespace imajuscule;
using namespace imajuscule::audio;
using namespace imajuscule::Sensor;

// no need to synchronize access to this : it's 4 bytes-aligned, amd only one thread writes it (the audio thread) except for initialization time

int32_t n_audio_cb_frames = initial_n_audio_cb_frames;

namespace imajuscule {
    namespace audio {
        int wait_for_first_n_audio_cb_frames() {
            // Note this could deadlock if there is an audio bug at os level
            while(n_audio_cb_frames == initial_n_audio_cb_frames) {
                std::this_thread::yield();
            }
            return n_audio_cb_frames;
        }
    }
}
#if TARGET_OS_IOS


OSStatus renderCallback_out(void                        *userData,
                            AudioUnitRenderActionFlags  *actionFlags,
                            const AudioTimeStamp        *audioTimeStamp,
                            UInt32                      busNumber,
                            UInt32                      numFrames,
                            AudioBufferList             *buffers) {
    
    n_audio_cb_frames = numFrames;
    
    auto ios_data = reinterpret_cast<iOSOutputData*>(userData);
    auto sizeBuffer = numFrames * AudioOut::nAudioOut;
    for (UInt32 i=0; i<buffers->mNumberBuffers; ++i) {
        // might be less than previous value (when using parot headphones in bluetooth)
        auto v = buffers->mBuffers[i].mDataByteSize / sizeof(SInt16);
        if(v < sizeBuffer) {
            LG(INFO, "diff %d", sizeBuffer-v);
            sizeBuffer = v;
        }
        break;
    }
    
    auto & outputBuffer = ios_data->buf;
    outputBuffer.resize(sizeBuffer); // hopefully we already reserved enough
    
    ios_data->data->step(outputBuffer.data(), numFrames);
    
    for (UInt32 i=0; i<buffers->mNumberBuffers; ++i) {
        Assert(sizeBuffer * sizeof(SInt16) <= buffers->mBuffers[i].mDataByteSize);
        Assert(AudioOut::nAudioOut == buffers->mBuffers[i].mNumberChannels);
        auto buffer = (SInt16*)(buffers->mBuffers[i].mData);
        for( UInt32 j=0; j<sizeBuffer; j++ ) {
            auto val = (SInt16)(outputBuffer[j] * 32767.f);
            *buffer = val;
            ++buffer;
        }
    }
    
    return noErr;
}
#else
static int playCallback( const void *inputBuffer, void *outputBuffer,
                          unsigned long numFrames,
                          const PaStreamCallbackTimeInfo* timeInfo,
                          PaStreamCallbackFlags statusFlags,
                          void *userData )
{
    n_audio_cb_frames = numFrames;

    outputData *data = (outputData*)userData;
    
    (void) outputBuffer;
    (void) timeInfo;
    (void) statusFlags;
    (void) userData;
    
    data->step((SAMPLE*)outputBuffer, static_cast<int>(numFrames));
    
    return paContinue;
}
#endif
void AudioOut::Init() {
    if(bInitialized) {
        return;
    }
    if(doInit()) {
        initializeConvolutionReverb();
    }
}

bool AudioOut::doInit() {
    LG(INFO, "AudioOut::doInit");
#if TARGET_OS_IOS
    bInitialized = true;
    if(0==initAudioSession())
    {
        if(0==initAudioStreams(audioUnit_out, data, renderCallback_out, nAudioOut, desc))
        {
            OSStatus res = startAudioUnit(audioUnit_out);
            if( noErr != res )
            {
                LG(ERR, "AudioOut::doInit : startAudioUnit failed : %d", res);
                Assert(0);
                return false;
            }
        }
        else
        {
            LG(ERR, "AudioOut::doInit : initAudioStreams failed");
            Assert(0);
            return false;
        }
    }
    else
    {
        LG(ERR, "AudioOut::doInit : initAudioSession failed");
        Assert(0);
        return false;
    }
#else
    
    LG(INFO, "AudioOut::doInit : initializing %s", Pa_GetVersionText());
    PaError err = Pa_Initialize();
    if(likely(err == paNoError)) {
        bInitialized = true;
        
        LG(INFO, "AudioOut::doInit : done initializing %s", Pa_GetVersionText());
        
        LG(INFO,"AudioOut::doInit : %d host apis", Pa_GetHostApiCount());
        
        PaStreamParameters p;
        p.device = Pa_GetDefaultOutputDevice();
        if (unlikely(p.device == paNoDevice)) {
            LG(ERR, "AudioOut::doInit : No default output device");
            Assert(0);
            return false;
        }
        LG(INFO, "AudioOut::doInit : audio device : id %d", p.device);
        
        p.channelCount = nAudioOut;
        p.sampleFormat = IMJ_PORTAUDIO_SAMPLE_TYPE;
        
        auto pi = Pa_GetDeviceInfo( p.device );
        LG(INFO, "AudioOut::doInit : audio device : hostApi    %d", pi->hostApi);
        LG(INFO, "AudioOut::doInit : audio device : name       %s", pi->name);
        LG(INFO, "AudioOut::doInit : audio device : maxIC      %d", pi->maxInputChannels);
        LG(INFO, "AudioOut::doInit : audio device : maxOC      %d", pi->maxOutputChannels);
        LG(INFO, "AudioOut::doInit : audio device : def. sr    %f", pi->defaultSampleRate);
        LG(INFO, "AudioOut::doInit : audio device : def. lolat %f", pi->defaultLowOutputLatency);
        LG(INFO, "AudioOut::doInit : audio device : def. holat %f", pi->defaultHighOutputLatency);
        
        p.suggestedLatency = /*4* trying to resolve crack at the beg*/nAudioOut *
        // on windows it's important to not set suggestedLatency too low, else samples are lost (for example only 16 are available per timestep)
        pi->defaultLowOutputLatency;
        
        p.hostApiSpecificStreamInfo = nullptr;
        
        /* Listen to some audio. -------------------------------------------- */
        PaError err = Pa_OpenStream(
                                    &stream,
                                    nullptr,
                                    &p,                  /* &outputParameters, */
                                    SAMPLE_RATE,
                                    0 /*if not 0 an additional buffering may be used for some host apis, increasing latency*/,
                                    paClipOff | paPrimeOutputBuffersUsingStreamCallback,      /* we won't output out of range samples so don't bother clipping them */
                                    playCallback,
                                    &data);
        if( unlikely(err != paNoError) )
        {
            stream = nullptr;
            LG(ERR, "AudioOut::doInit : Pa_OpenStream failed : %s", Pa_GetErrorText(err));
            Assert(0);
            return false;
        }
        
        const PaStreamInfo * si = Pa_GetStreamInfo(stream);
        
        LG(INFO, "AudioOut::doInit : stream : output lat  %f", si->outputLatency);
        LG(INFO, "AudioOut::doInit : stream : input lat   %f", si->inputLatency);
        LG(INFO, "AudioOut::doInit : stream : sample rate %f", si->sampleRate);
        
        err = Pa_StartStream( stream );
        if( unlikely(err != paNoError) )
        {
            LG(ERR, "AudioOut::doInit : Pa_StartStream failed : %s", Pa_GetErrorText(err));
            Assert(0);
            return false;
        }
    }
    else
    {
        LG(ERR, "AudioOut::doInit : PA_Initialize failed : %s", Pa_GetErrorText(err));
        Assert(0);
        return false;
    }
#endif
    
    LG(INFO, "AudioOut::doInit : success");
    return true;
}

void AudioOut::initializeConvolutionReverb()
{
    // for Wind app we want to let the user decide to have reverb
    audio::dontUseConvolutionReverb(data);

    // this one needs to be high pass filtered (5hz loud stuff)
/*    std::string dirname = std::string(impulse_responses_root_dir) + "/nyc.showroom";
    constexpr auto filename = "BigRoomStereo (16).wav";
    //std::string dirname = std::string(impulse_responses_root_dir) + "/im.reverbs";
    //constexpr auto filename = "Conic Long Echo Hall.wav";
    audio::useConvolutionReverb(data, dirname, filename);
  */  
}

void AudioOut::TearDown() {
    LG(INFO, "AudioOut::TearDown");
    
#if TARGET_OS_IOS
    if( bInitialized ) {
        bInitialized = false;
        OSStatus err = stopProcessingAudio(audioUnit_out);
        if( noErr != err ) {
            LG(ERR, "AudioOut::TearDown : stopProcessingAudio failed : %d", err);
            Assert(0);
            return;
        }
    }
#else
    if(stream)
    {
        PaError err = Pa_CloseStream( stream );
        stream = nullptr;
        if( unlikely(err != paNoError) ) {
            LG(ERR, "AudioOut::TearDown : Pa_CloseStream failed : %s", Pa_GetErrorText(err));
            Assert(0);
            return;
        }
    }
    
    if( bInitialized ) { // don't call Pa_Terminate if Pa_Initialize failed
        bInitialized = false;

        PaError err = Pa_Terminate();
        if(err != paNoError)
        {
            LG(ERR, "AudioOut::TearDown : PA_Terminate failed : %s", Pa_GetErrorText(err));
            return;
        }
    }
#endif
    
    LG(INFO, "AudioOut::TearDown : success");
}
