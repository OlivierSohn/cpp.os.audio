
using namespace imajuscule;
using namespace imajuscule::Sensor;

#ifdef _WIN32
int setenv(const char *name, const char *value, int overwrite)
{
    int errcode = 0;
    if ( !overwrite ) {
        size_t envsize = 0;
        errcode = getenv_s(&envsize, nullptr, 0, name);
        if ( errcode || envsize ) return errcode;
    }
    return _putenv_s(name, value);
}
#endif


/* This routine will be called by the PortAudio engine when audio is needed.
 ** It may be called at interrupt level on some machines so don't do anything
 ** that could mess up the system like calling malloc() or free().
 */
#if TARGET_OS_IOS

struct iOSOutputData {
    // we cannot know for sure how much the os will ask us to compute.
    // on my iPhone 4s I observed 512 was asked.
    static constexpr auto initial_buffer_size = 1024;
    
    iOSOutputData() {
        // preallocate to avoid dynamic allocation in audio thread
        buf.reserve(initial_buffer_size);
    }
    
    outputData * data = nullptr;
    std::vector<outputData::T> buf;
};

AudioUnit audioUnit_in = nullptr;
AudioUnit audioUnit_out = nullptr;
OSStatus renderCallback_in(void                        *userData,
                           AudioUnitRenderActionFlags  *actionFlags,
                           const AudioTimeStamp        *audioTimeStamp,
                           UInt32                      busNumber,
                           UInt32                      numFrames,
                           AudioBufferList             *buffers) {
    
    OSStatus status = AudioUnitRender(audioUnit_in,
                                      actionFlags,
                                      audioTimeStamp,
                                      1,
                                      numFrames,
                                      buffers);
    if(status != noErr) {
        LG(ERR,"renderCallback (audio) : error %d", status);
        A(0);
        return status;
    }
    
    paTestData *data = (paTestData*)userData;

    data->convertedSampleBuffer.resize(numFrames);
    float * buf = data->convertedSampleBuffer.data();
    
    SInt16 *inputFrames = (SInt16*)(buffers->mBuffers->mData);
    
    for(auto i = 0; i < numFrames; i++) {
        buf[i] = (float)inputFrames[i] / 32768.f;
    }
    
    data->step((const SAMPLE*)buf, numFrames);

    // mute audio
    for (UInt32 i=0; i<buffers->mNumberBuffers; ++i) {
        memset(buffers->mBuffers[i].mData, 0, buffers->mBuffers[i].mDataByteSize);
    }
    
    return noErr;
}
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
        LG(ERR,"renderCallback (audio) : error %d", status);
        A(0);
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
static int recordCallback( const void *inputBuffer, void *outputBuffer,
                          unsigned long nFrames,
                          const PaStreamCallbackTimeInfo* timeInfo,
                          PaStreamCallbackFlags statusFlags,
                          void *userData )
{
    paTestData *data = (paTestData*)userData;

    (void) outputBuffer;
    (void) timeInfo;
    (void) statusFlags;
    (void) userData;
    
    data->step((const SAMPLE*)inputBuffer, (int)nFrames);
    
    return paContinue;
}
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
void paTestData::step(const SAMPLE *rptr, int nFrames)
{
    RAIILock l(used);
    
    if( !activator.onStep() && rptr )
    {
        for( int i=0; i<nFrames; i++ )
        {
            auto val = *rptr++;
            algo_max.feed(val);
            
            // filter high frequencies
            avg.feed(val);
            auto filtered_value = avg.compute();
            algo_freq.feed(filtered_value);
        }
    }
    else {
        algo_max.forget();
        algo_freq.forget();
    }
}


Audio * Audio::gInstance = nullptr;
Audio * Audio::getInstance()
{
    return Globals::ptr<Audio>(gInstance);
}


void Audio::Init() {
    if(auto i = Audio::getInstance()) {
        i->doInit();
    }
}

void Audio::TearDown() {
    if(auto i = Audio::getInstance()) {
        i->doTearDown();
    }
}

#if TARGET_OS_IOS
int initAudioSession() {
    
    if(AudioSessionInitialize(nullptr, nullptr, nullptr, nullptr) != noErr) {
        return 1;
    }
    
    if(AudioSessionSetActive(true) != noErr) {
        return 1;
    }
    
    UInt32 sessionCategory = kAudioSessionCategory_PlayAndRecord;
    if(AudioSessionSetProperty(kAudioSessionProperty_AudioCategory,
                               sizeof(UInt32), &sessionCategory) != noErr) {
        return 1;
    }
    
    Float32 bufferSizeInSec = 0.005f;
    if(AudioSessionSetProperty(kAudioSessionProperty_PreferredHardwareIOBufferDuration,
                               sizeof(Float32), &bufferSizeInSec) != noErr) {
        return 1;
    }
    
    UInt32 overrideCategory = 1;
    if(AudioSessionSetProperty(kAudioSessionProperty_OverrideCategoryDefaultToSpeaker,
                               sizeof(UInt32), &overrideCategory) != noErr) {
        return 1;
    }
    
    // There are many properties you might want to provide callback functions for:
    // kAudioSessionProperty_AudioRouteChange
    // kAudioSessionProperty_OverrideCategoryEnableBluetoothInput
    // etc.
    
    return 0;
}

int initAudioStreams(AudioUnit & audioUnit, void * data, AURenderCallback cb, int nOuts) {
    UInt32 audioCategory = //kAudioSessionCategory_RecordAudio;
    kAudioSessionCategory_PlayAndRecord;
    if(AudioSessionSetProperty(kAudioSessionProperty_AudioCategory,
                               sizeof(UInt32), &audioCategory) != noErr) {
        return 1;
    }
    /*
    UInt32 overrideCategory = 1;
    if(AudioSessionSetProperty(kAudioSessionProperty_OverrideCategoryDefaultToSpeaker,
                               sizeof(UInt32), &overrideCategory) != noErr) {
        // Less serious error, but you may want to handle it and bail here
    }*/
    
    AudioComponentDescription componentDescription;
    componentDescription.componentType = kAudioUnitType_Output;
    componentDescription.componentSubType = kAudioUnitSubType_RemoteIO;
    componentDescription.componentManufacturer = kAudioUnitManufacturer_Apple;
    componentDescription.componentFlags = 0;
    componentDescription.componentFlagsMask = 0;
    auto component = AudioComponentFindNext(nullptr, &componentDescription);
    if(AudioComponentInstanceNew(component, &audioUnit) != noErr) {
        return 1;
    }
    
    UInt32 enable = 1;
    if(AudioUnitSetProperty(audioUnit, kAudioOutputUnitProperty_EnableIO,
                            kAudioUnitScope_Input, 1, &enable, sizeof(UInt32)) != noErr) {
        return 1;
    }
    
    AURenderCallbackStruct callbackStruct;
    callbackStruct.inputProc = cb;
    callbackStruct.inputProcRefCon = data;
    if(AudioUnitSetProperty(audioUnit, kAudioUnitProperty_SetRenderCallback,
                            kAudioUnitScope_Input, 0, &callbackStruct,
                            sizeof(AURenderCallbackStruct)) != noErr) {
        return 1;
    }
    
    AudioStreamBasicDescription streamDescription;
    // You might want to replace this with a different value, but keep in mind that the
    // iPhone does not support all sample rates. 8kHz, 22kHz, and 44.1kHz should all work.
    streamDescription.mSampleRate = SAMPLE_RATE;
    // Yes, I know you probably want floating point samples, but the iPhone isn't going
    // to give you floating point data. You'll need to make the conversion by hand from
    // linear PCM <-> float.
    streamDescription.mFormatID = kAudioFormatLinearPCM;
    // This part is important!
    streamDescription.mFormatFlags = kAudioFormatFlagIsSignedInteger |
    kAudioFormatFlagsNativeEndian |
    kAudioFormatFlagIsPacked;
    // Not sure if the iPhone supports recording >16-bit audio, but I doubt it.
    streamDescription.mBitsPerChannel = 16;
    streamDescription.mBytesPerFrame = nOuts*sizeof(SInt16);
    streamDescription.mChannelsPerFrame = nOuts;
    streamDescription.mFramesPerPacket = 1;
    streamDescription.mBytesPerPacket = streamDescription.mBytesPerFrame * streamDescription.mFramesPerPacket;
    streamDescription.mReserved = 0;
    
    if(AudioUnitSetProperty(audioUnit, kAudioUnitProperty_StreamFormat,
                            kAudioUnitScope_Input, 0, &streamDescription, sizeof(streamDescription)) != noErr) {
        return 1;
    }
    
    if(AudioUnitSetProperty(audioUnit, kAudioUnitProperty_StreamFormat,
                            kAudioUnitScope_Output, 1, &streamDescription, sizeof(streamDescription)) != noErr) {
        return 1;
    }
    
    return 0;
}

int initAudioStreams(AudioUnit & audioUnit, imajuscule::Sensor::paTestData & data, AURenderCallback cb, int nOuts) {
    return initAudioStreams(audioUnit, &data, cb, nOuts);
}

int initAudioStreams(AudioUnit & audioUnit, outputData & data, AURenderCallback cb, int nOuts) {
    
    static iOSOutputData ios_odata;
    A(!ios_odata.data || (ios_odata.data==&data));
    ios_odata.data = &data;
    return initAudioStreams(audioUnit, &ios_odata, cb, nOuts);
}


OSStatus startAudioUnit(AudioUnit audioUnit) {
    OSStatus res = AudioUnitInitialize(audioUnit);
    if( res != noErr ) {
        return res;
    }
    
    return AudioOutputUnitStart(audioUnit);
}

OSStatus stopProcessingAudio(AudioUnit audioUnit) {
    OSStatus res = AudioOutputUnitStop(audioUnit);
    if( res != noErr ) {
        return res;
    }
    
    res = AudioUnitUninitialize(audioUnit);
    if( res != noErr ) {
        return res;
    }
    
    audioUnit = nullptr;
    return noErr;
}

#endif

void Audio::doInit() {

#if TARGET_OS_IOS
#else
    // set minimum latency env var to speed things up
    const char * lat = "PA_MIN_LATENCY_MSEC";
    const char * latVal = "1";
    int erri = setenv( lat, latVal, true);
    if(unlikely(erri))
    {
        LG(ERR, "AudioIn::get : Could not set env variable PA_MIN_LATENCY_MSEC: %d", errno);
        A(0);
    }
    
    // verify that env var was set
#ifdef _WIN32
    char * test=0;
    size_t sz=0;
    if (0 != _dupenv_s(&test, &sz, lat) )
    {
        test = 0;
    }
#else
    const char * test = getenv (lat);
#endif
    
    A(test);
    A(!strcmp(test, latVal));
#ifdef _WIN32
    free(test);
#endif
#endif
    
    audioIn.Init();
}
void AudioIn::Init()
{
    if(bInitialized_)
    {
        LG(WARN, "AudioIn::Init already initialized");
        return;
    }
    
    data.algo_max.Register();
    data.algo_freq.Register();
    
    data.algo_max.setActivator(this);
    data.algo_freq.setActivator(this);

    bInitialized_ = true;
}

bool AudioIn::do_wakeup() {
    LG(INFO, "AudioIn::do_wakeup : AudioIn will wake up");
#if TARGET_OS_IOS
    if(0==initAudioSession())
    {
        if(0==initAudioStreams(audioUnit_in, data, renderCallback_in, 1))
        {
            OSStatus res;
            res = startAudioUnit(audioUnit_in);
            if( noErr != res )
            {
                LG(ERR, "AudioIn::do_wakeup : startAudioUnit failed : %d", res);
                A(0);
                return false;
            }
        }
        else
        {
            LG(ERR, "AudioIn::do_wakeup : initAudioStreams failed");
            A(0);
            return false;
        }
    }
    else
    {
        LG(ERR, "AudioIn::do_wakeup : initAudioSession failed");
        // fails on simulator
        //A(0);
        return false;
    }
#else

    LG(INFO, "AudioIn::do_wakeup : initializing %s", Pa_GetVersionText());
    auto err = Pa_Initialize();
    if(likely(err == paNoError))
    {
        LG(INFO, "AudioIn::do_wakeup : done initializing %s", Pa_GetVersionText());
        
        LG(INFO,"AudioIn::do_wakeup : %d host apis", Pa_GetHostApiCount());
        
        PaStreamParameters inputParameters;
        inputParameters.device = Pa_GetDefaultInputDevice();
        if (unlikely(inputParameters.device == paNoDevice)) {
            LG(ERR, "AudioIn::do_wakeup : No default input device");
            A(0);
            return false;
        }
        LG(INFO, "AudioIn::do_wakeup : audio device : id %d", inputParameters.device);
        
        inputParameters.channelCount = 1;
        inputParameters.sampleFormat = IMJ_PORTAUDIO_SAMPLE_TYPE;
        
        auto pi = Pa_GetDeviceInfo( inputParameters.device );
        LG(INFO, "AudioIn::do_wakeup : audio device : hostApi    %d", pi->hostApi);
        LG(INFO, "AudioIn::do_wakeup : audio device : name       %s", pi->name);
        LG(INFO, "AudioIn::do_wakeup : audio device : maxIC      %d", pi->maxInputChannels);
        LG(INFO, "AudioIn::do_wakeup : audio device : maxOC      %d", pi->maxOutputChannels);
        LG(INFO, "AudioIn::do_wakeup : audio device : def. sr    %f", pi->defaultSampleRate);
        LG(INFO, "AudioIn::do_wakeup : audio device : def. lilat %f", pi->defaultLowInputLatency);
        LG(INFO, "AudioIn::do_wakeup : audio device : def. hilat %f", pi->defaultHighInputLatency);

        inputParameters.suggestedLatency =
            // on windows it's important to not set suggestedLatency too low, else samples are lost (for example only 16 are available per timestep)
        pi->defaultLowInputLatency;

        inputParameters.hostApiSpecificStreamInfo = nullptr;
        
        /* Record some audio. -------------------------------------------- */
        PaError err = Pa_OpenStream(
                            &stream,
                            &inputParameters,
                            nullptr,                  /* &outputParameters, */
                            SAMPLE_RATE,
                            0 /*if not 0 an additional buffering may be used for some host apis, increasing latency*/,
                            paClipOff,      /* we won't output out of range samples so don't bother clipping them */
                            recordCallback,
                            &data);
        if( unlikely(err != paNoError) )
        {
            stream = nullptr;
            LG(ERR, "AudioIn::do_wakeup : Pa_OpenStream failed : %s", Pa_GetErrorText(err));
            A(0);
            return false;
        }
        
        const PaStreamInfo * si = Pa_GetStreamInfo(stream);
        
        LG(INFO, "AudioIn::do_wakeup : stream : output lat  %f", si->outputLatency);
        LG(INFO, "AudioIn::do_wakeup : stream : input lat   %f", si->inputLatency);
        LG(INFO, "AudioIn::do_wakeup : stream : sample rate %f", si->sampleRate);
        
        err = Pa_StartStream( stream );
        if( unlikely(err != paNoError) )
        {
            LG(ERR, "AudioIn::do_wakeup : Pa_StartStream failed : %s", Pa_GetErrorText(err));
            A(0);
            return false;
        }
    }
    else
    {
        LG(ERR, "AudioIn::do_wakeup : PA_Initialize failed : %s", Pa_GetErrorText(err));
        A(0);
        return false;
    }
#endif

    LG(INFO, "AudioIn::do_wakeup : AudioIn is woken up");
    return true;
}

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
bool AudioIn::do_sleep() {
    LG(INFO, "AudioIn::do_sleep : AudioIn will sleep");

#if TARGET_OS_IOS
    OSStatus err = stopProcessingAudio(audioUnit_in);
    if( noErr != err ) {
        LG(ERR, "AudioIn::do_sleep : stopProcessingAudio failed : %d", err);
        A(0);
        return false;
    }
#else
    if(stream)
    {
        PaError err = Pa_CloseStream( stream );
        stream = nullptr;
        if( unlikely(err != paNoError) ) {
            LG(ERR, "AudioIn::do_sleep : Pa_CloseStream failed : %s", Pa_GetErrorText(err));
            A(0);
            return false;
        }
    }
    
    PaError err = Pa_Terminate();
    if(unlikely(err != paNoError))
    {
        LG(ERR, "AudioIn::do_sleep : PA_Terminate failed : %s", Pa_GetErrorText(err));
        return false;
    }
#endif
    
    LG(INFO, "AudioIn::do_sleep : AudioIn sleeping");
    return true;
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

void AudioIn::TearDown()
{
    Activator::sleep();

    if(bInitialized_) {
        data.algo_freq.Unregister();
        data.algo_max.Unregister();
        
        bInitialized_ = false;
    }
    else {
        LG(ERR, "AudioIn::TearDown : was not initialized");
    }
}

void Audio::doTearDown() {
    audioOut.TearDown();
    audioIn.TearDown();
}

InternalResult AlgoMax::computeWhileLocked(float &f)
{
    f = maxAbsSinceLastRead;
    maxAbsSinceLastRead = 0.f;
    return InternalResult::COMPLETE_SUCCESS;
}
InternalResult AlgoMax::compute(float &f)
{
    A(0);
    return InternalResult::COMPLETE_ERROR;
}

static void plot(const char*title,const std::vector<SAMPLE>&v)
{
    return;
    LG(INFO,"%s", title);
    std::string s(v.size(), ' ');
    float min_ = 0.f;
    float max_ = 0.f;
    
    for(auto e:v)
    {
        min_ = std::min(min_, e);
        max_ = std::max(max_, e);
    }
    
    const int vSteps = 10;
    
    std::vector<std::string> vs(vSteps, s);
    int i=0;
    for(auto e:v)
    {
        auto normalized = (e - min_) / (max_ - min_);
        size_t index = (size_t)(normalized * (float)vSteps);
        if(index == vSteps)
            index = vSteps-1;
        vs[index][i] = '*';
        i++;
    }
    
    for(const auto & str:vs)
    {
        LG(INFO,"%s",str.c_str());
    }
    
}

InternalResult FreqFromZC::compute(float & f)
{
    A(0);
    return InternalResult::COMPLETE_ERROR;
}
InternalResult FreqFromZC::computeWhileLocked(float & f)
{
    auto delta = signal_range.delta();
    signal_range.set(0.f, 0.f);
    
    struct algo_freqFromZC
    {
        range<int32_t> periodRange;
        int32_t total = 0;
        int32_t count = 0;
        int32_t cur=0;
        bool hasStarted = false;
        std::function<void(int32_t)> step = [this](int32_t interval){
            if(!interval)
                return;
            
            if(!hasStarted)
            {
                if(periodRange.contains(interval))
                    hasStarted = true;
                else
                    return;
            }
            int32_t next = cur + interval;
            if(periodRange.contains(next))
            {
                total += next;
                count ++;
                cur = 0;
            }
            else if(periodRange.contains(next/2))
            {
                total += next;
                count += 2;
                cur = 0;
            }
            else if(next > periodRange.getMax() * 2)
                cur = 0;
            else
                cur = next;
        };
    } data;
    
    // use ranges : 200 202 203 201 302 99 should give the 200 range

    struct IntervalRange
    {
        IntervalRange(int32_t initiator) :
        initiator_(initiator)
        , count_(1){
            float jitter = ((float)initiator) * 0.15f;
            int32_t jitter_int = std::max(1, (int32_t)(jitter+0.5f));
            r_.set(initiator-jitter_int, initiator+jitter_int);
        }
        
        bool tryInsert(int32_t val)
        {
            if(r_.contains(val))
            {
                count_++;
                return true;
            }
            return false;
        }
        int32_t getCount() const { return count_;}
        int32_t getInitiator() const { return initiator_;}
        range<int32_t> const& getRange() const {return r_;}
    private:
        int32_t initiator_;
        range<int32_t> r_;
        int32_t count_;
    };
    StackVector<IntervalRange> ranges(positive_zeros_dist.size());
    
    auto it = positive_zeros_dist.begin();
    auto end = positive_zeros_dist.end();
    for(;it!=end;++it)
    {
        auto interval = *it;
        if(interval)
        {
            bool bFound = false;
            for(auto & r:ranges)
            {
                if( r.tryInsert(interval))
                {
                    bFound = true;
                    break;
                }
            }
            
            if(!bFound)
            {
                ranges.emplace_back(interval);
            }
        }
    }

    // ponderate frequency of occurences by initiator value so that 20 20 20 20 30 10 gives 20
    //      and so that 4 4 4 4 8 8 gives 8
    
    int32_t maxCount = 0;
    for(auto const& r:ranges)
    {
        auto count = r.getCount() * r.getInitiator();
        if(count > maxCount)
        {
            maxCount = count;
            data.periodRange = r.getRange();
        }
    }

    positive_zeros_dist.for_each(data.step);
    
    if(data.total)
    {
        // enable result if:
        // all periods were reconstructed from zero-crossing intervals
        // or the signal is loud enough to not be considered noise
        if(data.count == (int32_t)positive_zeros_dist.size() || delta > upperZero )
        {
            auto candidate = ((float)data.count) / ((float)data.total);
            candidate *= ((float)SAMPLE_RATE)/(float)sampling_period;
            if(candidate < minFreq )
            {
                //LG(INFO,"%.4f < %.4f : discarded", candidate, minFreq);
            }
            else if(candidate > maxFreq)
            {
                //LG(INFO,"%.4f > %.4f : discarded", candidate, maxFreq);
            }
            else
            {
                f = candidate;
                return InternalResult::COMPLETE_SUCCESS;
            }
        }
    }
    
    return InternalResult::COMPLETE_ERROR;
}
