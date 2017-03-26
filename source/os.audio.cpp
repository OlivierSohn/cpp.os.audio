
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
    
#ifndef NO_AUDIO_IN
    audioIn.Init();
#endif
}

void Audio::doTearDown() {
    audioOut.TearDown();
#ifndef NO_AUDIO_IN
    audioIn.TearDown();
#endif
}
