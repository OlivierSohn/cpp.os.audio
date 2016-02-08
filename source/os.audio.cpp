#ifdef __APPLE__
#include "TargetConditionals.h"
#endif

#include <stdlib.h>
#include <string.h>
#include <cerrno>
#include <algorithm>
#include <functional>
#include <cmath>
#include <map>
#include <cstdlib>

#if TARGET_OS_IOS
#import <AudioToolbox/AudioToolbox.h>
#else
// SDL does not have audio recording yet so I use portaudio
//#include "SDL_audio.h"
#include "portaudio.h"
#endif

// fft method is not easy to implement, i prefered zero crossing instead but i leave it here just in case
//#include "kiss_fftr.h"

#include "os.log.h"

#include "os.audio.h"

using namespace imajuscule;
using namespace imajuscule::Sensor;

#ifdef _WIN32
int setenv(const char *name, const char *value, int overwrite)
{
    int errcode = 0;
    if ( !overwrite ) {
        size_t envsize = 0;
        errcode = getenv_s(&envsize, NULL, 0, name);
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
AudioUnit audioUnit_in = NULL;
AudioUnit audioUnit_out = NULL;
std::vector<float> convertedSampleBuffer, outputBuffer;
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
    
    convertedSampleBuffer.resize(numFrames);
    float * buf = convertedSampleBuffer.data();
    
    SInt16 *inputFrames = (SInt16*)(buffers->mBuffers->mData);
    
    // If your DSP code can use integers, then don't bother converting to
    // floats here, as it just wastes CPU. However, most DSP algorithms rely
    // on floating point, and this is especially true if you are porting a
    // VST/AU to iOS.
    for(auto i = 0; i < numFrames; i++) {
        buf[i] = (float)inputFrames[i] / 32768.f;
    }
    
    paTestData *data = (paTestData*)userData;
    
    data->step((const SAMPLE*)buf, numFrames);

    // Now we have floating point sample data from the render callback! We
    // can send it along for further processing, for example:
    // plugin->processReplacing(convertedSampleBuffer, NULL, sampleFrames);
    
    
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
    outputBuffer.resize(numFrames);
    
    data->step(outputBuffer.data(), numFrames);
    
    for (UInt32 i=0; i<buffers->mNumberBuffers; ++i) {
        memset(buffers->mBuffers[i].mData, 0, buffers->mBuffers[i].mDataByteSize);
        for( int j=0; j<buffers->mBuffers[i].mDataByteSize; j++ ) {
            ((float*)(buffers->mBuffers[i].mData))[j] = (SInt16)(outputBuffer[j] * 32767.f);
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
    
    data->step((const SAMPLE*)inputBuffer, framesPerBuffer);
    
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
    
    data->step((SAMPLE*)outputBuffer, framesPerBuffer);
    
    return paContinue;
}
#endif
void paTestData::step(const SAMPLE *rptr, unsigned long framesPerBuffer)
{
    RAIILock l(used);
    
    if( !activator.onStep() && rptr )
    {
        for( unsigned long i=0; i<framesPerBuffer; i++ )
        {
            auto val = *rptr++;
            algo_max.feed(val);
            
            // filter high frequencies
            auto filtered_value = avg.feed(val);
            algo_freq.feed(filtered_value);
        }
    }
}


Audio * Audio::gInstance = NULL;
Audio& Audio::getInstance()
{
    if(!gInstance)
        gInstance = new Audio();
    return *gInstance;
}


#if TARGET_OS_IOS
int initAudioSession() {
    
    if(AudioSessionInitialize(NULL, NULL, NULL, NULL) != noErr) {
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
    AudioComponent component = AudioComponentFindNext(NULL, &componentDescription);
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
    streamDescription.mBytesPerFrame = 2;
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
    
    audioUnit = NULL;
    return noErr;
}

#endif

void Audio::Init() {

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
    
    if_A(test)
    {
        A(!strcmp(test, latVal));
#ifdef _WIN32
        free(test);
#endif
    }
#endif
    
    audioIn.Init();
    audioOut.Init();
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

        inputParameters.hostApiSpecificStreamInfo = NULL;
        
        /* Record some audio. -------------------------------------------- */
        PaError err = Pa_OpenStream(
                            &stream,
                            &inputParameters,
                            NULL,                  /* &outputParameters, */
                            SAMPLE_RATE,
                            0 /*if not 0 an additional buffering may be used for some host apis, increasing latency*/,
                            paClipOff,      /* we won't output out of range samples so don't bother clipping them */
                            recordCallback,
                            &data);
        if( unlikely(err != paNoError) )
        {
            stream = NULL;
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
    if(likely(err == paNoError))
    {
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
        
        p.hostApiSpecificStreamInfo = NULL;
        
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
            stream = NULL;
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
        if( unlikely(err != paNoError) ) {
            LG(ERR, "AudioOut::TearDown : Pa_CloseStream failed : %s", Pa_GetErrorText(err));
            A(0);
            return;
        }
    }
    
    if( bInitialized ) { // don't call Pa_Terminate if Pa_Initialize failed
        bInitialized = false;

        PaError err = Pa_Terminate();
        if(unlikely(err != paNoError))
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

    if(likely(bInitialized_))
    {
        data.algo_freq.Unregister();
        data.algo_max.Unregister();
        
        bInitialized_ = false;
    }
    else
    {
        LG(ERR, "AudioIn::TearDown : was not initialized");
    }
}

void Audio::TearDown() {
    audioOut.TearDown();
    audioIn.TearDown();
}

int AudioOut::openChannel() {
    return data.openChannel();
}
void AudioOut::closeChannel( int id ) {
    return data.closeChannel( id );
}

void AudioOut::play( int channel_id, Sound const & sound, float freq_hz, float duration_ms  ) {
    
    int nSamples = (int)( ((float)SAMPLE_RATE) * 0.001f * duration_ms );

    auto const & s = sounds.get( { sound, freq_hz } );
    
    if( sound.zeroOnPeriodBoundaries() ) {
        const int period_size = (int)s.values.size();
        
        const int mod = nSamples % period_size;
        if(mod) {
            nSamples += period_size-mod;
        }
        A( 0 == nSamples % period_size);
    }
    
    data.play( channel_id, s, nSamples );
}

int outputData::openChannel() {
    RAIILock l(used);
    
    channels.emplace_back();
    
    return channels.back().id;
}

void outputData::closeChannel(int channel_id) {
    RAIILock l(used);
    
    channels.erase(std::remove_if(channels.begin(),
                                  channels.end(),
                                  [=](const Channel & elt) { return elt.id == channel_id; } ),
                   channels.end());
}

void outputData::play( int channel_id, soundBuffer const & sound, int duration_in_samples ) {
    RAIILock l(used);
    
    bool bFound (false);
    for( auto & c : channels ) {
        if( channel_id == c.id ) {
            c.requests.emplace(sound, duration_in_samples);
            bFound = true;
            break;
        }
    }
    
    A(bFound);
}

int outputData::Channel::gId = 0;

void outputData::step(SAMPLE *outputBuffer, unsigned long framesPerBuffer) {
    
    memset(outputBuffer,0,framesPerBuffer*sizeof(SAMPLE));

    RAIILock l(used);

    for( auto & c: channels ) {
        c.step( outputBuffer, framesPerBuffer );
    }
}

void outputData::Channel::step(SAMPLE * outputBuffer, unsigned long framesPerBuffer)
{
    playing.consume(requests);
    
    playing.write( outputBuffer, framesPerBuffer );
}

void outputData::Channel::Playing::consume(std::queue<Request> & requests) {
    if( remaining_samples_count <= 0 ) {
        
        if (!requests.empty())
        {
            auto & request = requests.front();
            
            play(request);
            
            requests.pop();
        }
    }
}
void outputData::Channel::Playing::play(Request & request) {
    A( request.samples_count > 0 );
    
    sound = &request.sound;
    remaining_samples_count = request.samples_count;
    next_sample_index = 0;
}
const float amplitude = 0.1f; // ok to have 10 chanels at max amplitude at the same time
void outputData::Channel::Playing::write(SAMPLE * outputBuffer, unsigned long framesPerBuffer) {
    auto s = -1;
    
    for( unsigned long i=0; i<framesPerBuffer && remaining_samples_count > 0; i++ ) {
        if( s == -1 ) {
            s = (int) sound->values.size();
        }
        if( next_sample_index >= s ) {
            next_sample_index = 0;
        }
        
        *outputBuffer += amplitude * sound->values[next_sample_index];
        ++outputBuffer;
        ++next_sample_index;
        --remaining_samples_count;
    }
}

static float triangle( float angle_radians ) {
    A(angle_radians >= 0.f);
    A(angle_radians <= 2.f * (float)M_PI);
    
    static const float inv_pi = 1.f / (float)M_PI;
    
    angle_radians *= inv_pi;
    if( angle_radians < 0.5f ) {        // 0 .. 0.5   ->  0 .. 1
        return 2.f * angle_radians;
    } else if( angle_radians < 1.5f ) { // 0.5 .. 1.5 ->  1 .. -1
        return 2.f - 2.f * angle_radians;
    } else {                            // 1.5 .. 2   ->  -1 .. 0
        A( angle_radians <= 2.f );
        return -4.f + 2.f * angle_radians;
    }
}
static float saw( float angle_radians ) {
    A(angle_radians >= 0.f);
    A(angle_radians <= 2.f * (float)M_PI);
    
    static const float inv_pi = 1.f / (float)M_PI;
    
    angle_radians *= inv_pi;
    
    // 0 .. 2 -> 1 .. -1
    return 1.f - angle_radians;
}
static float square( float angle_radians ) {
    A(angle_radians >= 0.f);
    A(angle_radians <= 2.f * (float)M_PI);
    
    static const float inv_pi = 1.f / (float)M_PI;
    
    angle_radians *= inv_pi;
    
    if( angle_radians <= 1.f ) { // 0 .. 1 ->  1
        return 1.f;
    } else {                    // 1 .. 2 ->  -1
        A(angle_radians <= 2.f);
        return -1.f;
    }
}

static float my_rand(float) {
    float between_zero_one = (float)rand()/(float)(RAND_MAX);
    return (between_zero_one * 2.f) - 1.f;
}

template < typename F >
void soundBuffer::generate( int period, F f ) {
    
    values.reserve( period );
    
    // Let's compute the waveform. First sample is non zero, last sample is zero, so the mapping is:
    //
    //  sample(int) [0 .. period - 1]  ->  radian(float) [2*pi/period .. 2*pi]
    //
    float increment = 2.f * (float)M_PI / (float) period;
    
    for( int i=0; i<period;) {
        i++;
        values.emplace_back( f( increment * (float)i ) );
    }
    
    A( (int)values.size() == period );
}
soundBuffer::soundBuffer( soundId const & id ) {
    switch (id.sound.type) {
        case Sound::NOISE:
        {
            generate( id.period_length, my_rand );
            if( id.period_length < 20 ) {
                // fix for small number of random values
                {
                    // center around zero
                    
                    auto avg(0.f);
                    for( auto const & v : values ) {
                        avg += v;
                    }
                    avg /= (float)values.size();
                    for( auto & v : values ) {
                        v -= avg;
                    }
                }
                {
                    // maximize
                    
                    auto M(0.f);
                    for (auto const & v : values) {
                        M = std::max( M, std::abs( v ) );
                    }
                    if( M < 0.5 ) {
                        auto fact = 0.7f/M;
                        for( auto & v : values ) {
                            v *= fact;
                        }
                    }
                }
            }
            break;
        }

        case Sound::SINE:
            generate( id.period_length, sinf );
            break;
            
        case Sound::TRIANGLE:
            generate( id.period_length, triangle );
            break;
            
        case Sound::SAW:
            generate( id.period_length, saw );
            break;
            
        case Sound::SQUARE:
            generate( id.period_length, square );
            break;
            
        default:
            A(0);
            break;
    }
}

soundBuffer const & AudioOut::Sounds::get(soundId const & id ) {
    {
        auto it = sounds.find(id);
        if( it != sounds.end() ) {
            return it->second;
        }
    }
    auto it = sounds.emplace(id, id);
    A(it.second);
    return it.first->second;
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

/*
void FreqFromAutocorr::computeWhileLocked()
{
    processing.clear();
    processing.swap(willProcess);
}*/

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
/*
auto FreqFromAutocorr::computeFrequency(float & f) -> Result
{
    if(processing.empty())
        return Result::NOT_ENOUGH_DATA;
    
    /////////////////////////
    // substract mean value
    /////////////////////////
    
    {
        auto mean_value = mean();
        
        for(auto & v:processing)
        {
            v -= mean_value;
        }
    }
    
    plot("mean substracted", processing);
    
    /////////////////////////
    // multiply by hanning window
    /////////////////////////
    
    std::transform(processing.begin(), processing.end(),
                   hanning.begin(),
                   processing.begin(),
                   std::multiplies<SAMPLE>());
    
    plot("windowed", processing);

    ////////////////////////
    // pad with 0s
    ////////////////////////
    
    {
        size_t i = 0;
        for(; i<bufferLength; i++)
        {
            withZeroPading[i] = processing[i];
        }
        for(;i<bufferWithPaddingLength; i++)
        {
            withZeroPading[i] = 0.f;
        }
    }
    
    plot("padded", withZeroPading);

    ////////////////////////
    // compute FFT
    ////////////////////////
    
    {
        if(!cfg)
        {
            cfg = kiss_fftr_alloc(bufferWithPaddingLength, 0, NULL, NULL);
        }
        
        if(cfg)
        {
            kiss_fftr((kiss_fftr_cfg)cfg, withZeroPading.data(), (kiss_fft_cpx*)fft1.data());
        }
        else
        {
            LG(ERR, "not enough memory?");
            return Result::NOT_ENOUGH_MEMORY;
        }
    }
    
    ////////////////////////
    // square in frequenty domain
    ////////////////////////
    
    {
        // (a+bi)^2 = a^2 + 2abi - b^2
        for(auto & c : fft1)
        {
            c.r = c.r * c.r + c.i * c.i;
            c.i = 0;
        }
    }
    
    ////////////////////////
    // compute FFT2
    ////////////////////////
    
    {
        if(!cfg2)
        {
            cfg2 = kiss_fftr_alloc(bufferWithPaddingLength, 1, NULL, NULL);
        }
        
        if(cfg2)
        {
            kiss_fftri((kiss_fftr_cfg)cfg2, (const kiss_fft_cpx*)fft1.data(), withZeroPading.data());
        }
        else
        {
            LG(ERR, "not enough memory?");
            return Result::NOT_ENOUGH_MEMORY;
        }
    }

    plot("padded after ffts", withZeroPading);

    /////////////////////////
    // divide by hanning window autocorrelation
    /////////////////////////
    
    std::transform(withZeroPading.begin(), withZeroPading.end(),
                   inv_hanning_autocorr.begin(),
                   withZeroPading.begin(),
                   std::multiplies<SAMPLE>());
    plot("compensation", withZeroPading);

    f = 0.f;
    SAMPLE peak = 0.f;
    for(int i=bufferLength; i>bufferLength/2; --i)
    {
        int cmp = bufferLength-i;
        withZeroPading[i] *= inv_hanning_autocorr[cmp];
        if(withZeroPading[i]>peak)
        {
            f = (float )cmp;
            peak = withZeroPading[i];
        }
    }
    LG(INFO,"%.4f", f);
    plot("divide by hauto", withZeroPading);
    
    // when divide by hanning autocorr, the last tiny part of the spectrum goes wild, maybe that was why there was an upsampling in the algorithm

    return Result::OK;
}

FreqFromAutocorr::~FreqFromAutocorr()
{
    if( cfg )
        free( cfg );
    if( cfg2 )
        free( cfg2 );
}*/

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
