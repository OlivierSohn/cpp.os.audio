
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


const int NUM_CHANNELS(1);

#define DITHER_FLAG     (0)

#define CHECK_OVERFLOW  (0)
#define CHECK_UNDERFLOW  (0)



/* This routine will be called by the PortAudio engine when audio is needed.
 ** It may be called at interrupt level on some machines so don't do anything
 ** that could mess up the system like calling malloc() or free().
 */
#if TARGET_OS_IOS
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
    
    // If your DSP code can use integers, then don't bother converting to
    // floats here, as it just wastes CPU. However, most DSP algorithms rely
    // on floating point, and this is especially true if you are porting a
    // VST/AU to iOS.
    for(auto i = 0; i < numFrames; i++) {
        buf[i] = (float)inputFrames[i] / 32768.f;
    }
    
    data->step((const SAMPLE*)buf, numFrames);

    // Now we have floating point sample data from the render callback! We
    // can send it along for further processing, for example:
    // plugin->processReplacing(convertedSampleBuffer, nullptr, sampleFrames);
    
    
    /*
    // Assuming that you have processed in place, we can now write the
    // floating point data back to the input buffer.
    for(int i = 0; i < numFrames; i++) {
        // Note that we multiply by 32767 here, NOT 32768. This is to avoid
        // overflow errors (and thus clipping).
        inputFrames[i] = (SInt16)(convertedSampleBuffer[i] * 32767.f);
    }*/

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
    
    outputData *data = (outputData*)userData;
    data->outputBuffer.resize(numFrames);
    
    data->step(data->outputBuffer.data(), numFrames);
    
    for (UInt32 i=0; i<buffers->mNumberBuffers; ++i) {
        memset(buffers->mBuffers[i].mData, 0, buffers->mBuffers[i].mDataByteSize);
        A(numFrames * sizeof(SInt16) == buffers->mBuffers[i].mDataByteSize);
        for( UInt32 j=0; j<buffers->mBuffers[i].mDataByteSize; j++ ) {
            ((SInt16*)(buffers->mBuffers[i].mData))[j] = (SInt16)(data->outputBuffer[j] * 32767.f);
        }
    }
    
    return noErr;
}
#else
static int recordCallback( const void *inputBuffer, void *outputBuffer,
                          unsigned long framesPerBuffer,
                          const PaStreamCallbackTimeInfo* timeInfo,
                          PaStreamCallbackFlags statusFlags,
                          void *userData )
{
    paTestData *data = (paTestData*)userData;

    (void) outputBuffer;
    (void) timeInfo;
    (void) statusFlags;
    (void) userData;
    
    data->step((const SAMPLE*)inputBuffer, (int)framesPerBuffer);
    
    return paContinue;
}
static int playCallback( const void *inputBuffer, void *outputBuffer,
                          unsigned long framesPerBuffer,
                          const PaStreamCallbackTimeInfo* timeInfo,
                          PaStreamCallbackFlags statusFlags,
                          void *userData )
{
    outputData *data = (outputData*)userData;
    
    (void) outputBuffer;
    (void) timeInfo;
    (void) statusFlags;
    (void) userData;
    
    data->step((SAMPLE*)outputBuffer, (int)framesPerBuffer);
    
    return paContinue;
}
#endif
void paTestData::step(const SAMPLE *rptr, int framesPerBuffer)
{
    RAIILock l(used);
    
    if( !activator.onStep() && rptr )
    {
        for( int i=0; i<framesPerBuffer; i++ )
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

int initAudioStreams(AudioUnit & audioUnit, void * pData, AURenderCallback cb) {
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
    AudioComponent component = AudioComponentFindNext(nullptr, &componentDescription);
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
    callbackStruct.inputProcRefCon = pData;
    if(AudioUnitSetProperty(audioUnit, kAudioUnitProperty_SetRenderCallback,
                            kAudioUnitScope_Input, 0, &callbackStruct,
                            sizeof(AURenderCallbackStruct)) != noErr) {
        return 1;
    }
    
    AudioStreamBasicDescription streamDescription;
    // You might want to replace this with a different value, but keep in mind that the
    // iPhone does not support all sample rates. 8kHz, 22kHz, and 44.1kHz should all work.
    streamDescription.mSampleRate = 44100;
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
    // 1 sample per frame, will always be 2 as long as 16-bit samples are being used
    streamDescription.mBytesPerFrame = sizeof(SInt16);
    // Record in mono. Use 2 for stereo, though I don't think the iPhone does true stereo recording
    streamDescription.mChannelsPerFrame = 1;
    streamDescription.mBytesPerPacket = streamDescription.mBytesPerFrame *
    streamDescription.mChannelsPerFrame;
    // Always should be set to 1
    streamDescription.mFramesPerPacket = 1;
    // Always set to 0, just to be sure
    streamDescription.mReserved = 0;
    
    // Set up input stream with above properties
    if(AudioUnitSetProperty(audioUnit, kAudioUnitProperty_StreamFormat,
                            kAudioUnitScope_Input, 0, &streamDescription, sizeof(streamDescription)) != noErr) {
        return 1;
    }
    
    // Ditto for the output stream, which we will be sending the processed audio to
    if(AudioUnitSetProperty(audioUnit, kAudioUnitProperty_StreamFormat,
                            kAudioUnitScope_Output, 1, &streamDescription, sizeof(streamDescription)) != noErr) {
        return 1;
    }
    
    return 0;
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
        if(0==initAudioStreams(audioUnit_in, &data, renderCallback_in))
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
        A(0);
        return false;
    }
#else

    LG(INFO, "AudioIn::do_wakeup : initializing %s", Pa_GetVersionText());
    PaError err = Pa_Initialize();
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
        
        inputParameters.channelCount = NUM_CHANNELS;
        inputParameters.sampleFormat = PA_SAMPLE_TYPE;
        
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
        if(0==initAudioStreams(audioUnit_out, &data, renderCallback_out))
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
        
        p.channelCount = NUM_CHANNELS;
        p.sampleFormat = PA_SAMPLE_TYPE;
        
        auto pi = Pa_GetDeviceInfo( p.device );
        LG(INFO, "AudioOut::Init : audio device : hostApi    %d", pi->hostApi);
        LG(INFO, "AudioOut::Init : audio device : name       %s", pi->name);
        LG(INFO, "AudioOut::Init : audio device : maxIC      %d", pi->maxInputChannels);
        LG(INFO, "AudioOut::Init : audio device : maxOC      %d", pi->maxOutputChannels);
        LG(INFO, "AudioOut::Init : audio device : def. sr    %f", pi->defaultSampleRate);
        LG(INFO, "AudioOut::Init : audio device : def. lilat %f", pi->defaultLowInputLatency);
        LG(INFO, "AudioOut::Init : audio device : def. hilat %f", pi->defaultHighInputLatency);
        
        p.suggestedLatency =
        // on windows it's important to not set suggestedLatency too low, else samples are lost (for example only 16 are available per timestep)
        pi->defaultLowInputLatency;
        
        p.hostApiSpecificStreamInfo = nullptr;
        
        /* Record some audio. -------------------------------------------- */
        PaError err = Pa_OpenStream(
                                    &stream,
                                    nullptr,
                                    &p,                  /* &outputParameters, */
                                    SAMPLE_RATE,
                                    0 /*if not 0 an additional buffering may be used for some host apis, increasing latency*/,
                                    paClipOff,      /* we won't output out of range samples so don't bother clipping them */
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

uint8_t AudioOut::openChannel() {
    Init();
    return data.openChannel();
}
void AudioOut::closeChannel( uint8_t id ) {
    if( data.closeChannel( id ) ) {
        TearDown();
    }
}

void AudioOut::play( uint8_t channel_id, std::vector<Request> && v ) {
    data.play( channel_id, std::move( v ) );
}

void AudioOut::setVolume( uint8_t channel_id, float v ) {
    data.setVolume( channel_id, v);
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
        range<int> periodRange;
        int total = 0;
        int count = 0;
        int cur=0;
        bool hasStarted = false;
        std::function<void(int)> step = [this](int interval){
            if(!interval)
                return;
            
            if(!hasStarted)
            {
                if(periodRange.contains(interval))
                    hasStarted = true;
                else
                    return;
            }
            int next = cur + interval;
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
        IntervalRange(int initiator) :
        initiator_(initiator)
        , count_(1){
            float jitter = ((float)initiator) * 0.15f;
            int jitter_int = std::max(1, (int)(jitter+0.5f));
            r_.set(initiator-jitter_int, initiator+jitter_int);
        }
        
        bool tryInsert(int val)
        {
            if(r_.contains(val))
            {
                count_++;
                return true;
            }
            return false;
        }
        int getCount() const { return count_;}
        int getInitiator() const { return initiator_;}
        range<int> const& getRange() const {return r_;}
    private:
        int initiator_;
        range<int> r_;
        int count_;
    };
    std::vector<IntervalRange> ranges;
    
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
    
    int maxCount = 0;
    for(auto const& r:ranges)
    {
        auto count = r.getCount() * r.getInitiator();
        if(count > maxCount)
        {
            maxCount = count;
            data.periodRange = r.getRange();
        }
    }

    auto cycleStart = positive_zeros_dist.cycleEnd();
    for_each(cycleStart, end, data.step);
    for_each(positive_zeros_dist.begin(), cycleStart, data.step);

    if(data.total)
    {
        // enable result if:
        // all periods were reconstructed from zero-crossing intervals
        // or the signal is loud enough to not be considered noise
        if(data.count == (int)positive_zeros_dist.size() || delta > upperZero )
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
