
using namespace imajuscule;
using namespace imajuscule::Sensor;


#if TARGET_OS_IOS

AudioUnit audioUnit_out = nullptr;

OSStatus renderCallback_out(void                        *userData,
                            AudioUnitRenderActionFlags  *actionFlags,
                            const AudioTimeStamp        *audioTimeStamp,
                            UInt32                      busNumber,
                            UInt32                      numFrames,
                            AudioBufferList             *buffers) {
    
    OSStatus status = AudioUnitRender(audioUnit_out,
                                      actionFlags,
                                      audioTimeStamp,
                                      1,
                                      numFrames,
                                      buffers);
    if(status != noErr) {
        if(status == kAudioUnitErr_CannotDoInCurrentContext) {
            LG(ERR, "the app probably went in the background, need to return something else?");
        }
        LG(ERR,"renderCallback (audio) : error %d", status);
        return status;
    }
    
    auto ios_data = reinterpret_cast<iOSOutputData*>(userData);
    auto sizeBuffer = numFrames * AudioOut::nAudioOut;
    auto & outputBuffer = ios_data->buf;
    outputBuffer.resize(sizeBuffer); // hopefully we already reserved enough
    
    ios_data->data->step(outputBuffer.data(), numFrames);
    
    for (UInt32 i=0; i<buffers->mNumberBuffers; ++i) {
        A(sizeBuffer * sizeof(SInt16) == buffers->mBuffers[i].mDataByteSize);
        A(AudioOut::nAudioOut == buffers->mBuffers[i].mNumberChannels);
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
                          unsigned long nFrames,
                          const PaStreamCallbackTimeInfo* timeInfo,
                          PaStreamCallbackFlags statusFlags,
                          void *userData )
{
    outputData *data = (outputData*)userData;
    
    (void) outputBuffer;
    (void) timeInfo;
    (void) statusFlags;
    (void) userData;
    
    data->step((SAMPLE*)outputBuffer, (int)nFrames);
    
    return paContinue;
}
#endif

void AudioOut::Init() {
    if(bInitialized) {
        return;
    }
    LG(INFO, "AudioOut::Init");
#if TARGET_OS_IOS
    bInitialized = true;
    if(0==initAudioSession())
    {
        if(0==initAudioStreams(audioUnit_out, data, renderCallback_out, nAudioOut))
        {
            OSStatus res = startAudioUnit(audioUnit_out);
            if( noErr != res )
            {
                LG(ERR, "AudioOut::Init : startAudioUnit failed : %d", res);
                A(0);
                return;
            }
        }
        else
        {
            LG(ERR, "AudioOut::Init : initAudioStreams failed");
            A(0);
            return;
        }
    }
    else
    {
        LG(ERR, "AudioOut::Init : initAudioSession failed");
        A(0);
        return;
    }
#else
    
    LG(INFO, "AudioOut::Init : initializing %s", Pa_GetVersionText());
    PaError err = Pa_Initialize();
    if(likely(err == paNoError)) {
        bInitialized = true;
        
        LG(INFO, "AudioOut::Init : done initializing %s", Pa_GetVersionText());
        
        LG(INFO,"AudioOut::Init : %d host apis", Pa_GetHostApiCount());
        
        PaStreamParameters p;
        p.device = Pa_GetDefaultOutputDevice();
        if (unlikely(p.device == paNoDevice)) {
            LG(ERR, "AudioOut::Init : No default output device");
            A(0);
            return;
        }
        LG(INFO, "AudioOut::Init : audio device : id %d", p.device);
        
        p.channelCount = nAudioOut;
        p.sampleFormat = IMJ_PORTAUDIO_SAMPLE_TYPE;
        
        auto pi = Pa_GetDeviceInfo( p.device );
        LG(INFO, "AudioOut::Init : audio device : hostApi    %d", pi->hostApi);
        LG(INFO, "AudioOut::Init : audio device : name       %s", pi->name);
        LG(INFO, "AudioOut::Init : audio device : maxIC      %d", pi->maxInputChannels);
        LG(INFO, "AudioOut::Init : audio device : maxOC      %d", pi->maxOutputChannels);
        LG(INFO, "AudioOut::Init : audio device : def. sr    %f", pi->defaultSampleRate);
        LG(INFO, "AudioOut::Init : audio device : def. lolat %f", pi->defaultLowOutputLatency);
        LG(INFO, "AudioOut::Init : audio device : def. holat %f", pi->defaultHighOutputLatency);
        
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
            LG(ERR, "AudioOut::Init : Pa_OpenStream failed : %s", Pa_GetErrorText(err));
            A(0);
            return;
        }
        
        const PaStreamInfo * si = Pa_GetStreamInfo(stream);
        
        LG(INFO, "AudioOut::Init : stream : output lat  %f", si->outputLatency);
        LG(INFO, "AudioOut::Init : stream : input lat   %f", si->inputLatency);
        LG(INFO, "AudioOut::Init : stream : sample rate %f", si->sampleRate);
        
        err = Pa_StartStream( stream );
        if( unlikely(err != paNoError) )
        {
            LG(ERR, "AudioOut::Init : Pa_StartStream failed : %s", Pa_GetErrorText(err));
            A(0);
            return;
        }
    }
    else
    {
        LG(ERR, "AudioOut::Init : PA_Initialize failed : %s", Pa_GetErrorText(err));
        A(0);
        return;
    }
#endif
    
    LG(INFO, "AudioOut::Init : success");
}

void AudioOut::TearDown() {
    LG(INFO, "AudioOut::TearDown");
    
#if TARGET_OS_IOS
    if( bInitialized ) {
        bInitialized = false;
        OSStatus err = stopProcessingAudio(audioUnit_out);
        if( noErr != err ) {
            LG(ERR, "AudioOut::TearDown : stopProcessingAudio failed : %d", err);
            A(0);
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
            A(0);
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
