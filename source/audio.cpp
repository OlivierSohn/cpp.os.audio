#ifdef __APPLE__
#include "TargetConditionals.h"
#endif

#include <stdlib.h> // pulls in declaration of malloc, free
#include <string.h>
#include <cerrno>
#include <algorithm>
#include <functional>
#include <cmath>
#include <map>

#include "audio.h"

#if TARGET_OS_IOS
#import <AudioToolbox/AudioToolbox.h>
#else
// SDL does not have audio recording yet so I use portaudio
//#include "SDL_audio.h"
#include "portaudio.h"
#endif

// fft method is not easy to implement, i prefered zero crossing instead but i leave it here just in case
//#include "kiss_fftr.h"
#include <cstdlib>
#include "os.log.h"
using namespace imajuscule;

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


const int FRAMES_PER_BUFFER(16);
const float NUM_SECONDS(0.001f);
const int NUM_CHANNELS(1);

#define DITHER_FLAG     (0)

#define CHECK_OVERFLOW  (0)
#define CHECK_UNDERFLOW  (0)



/* This routine will be called by the PortAudio engine when audio is needed.
 ** It may be called at interrupt level on some machines so don't do anything
 ** that could mess up the system like calling malloc() or free().
 */
#if TARGET_OS_IOS
AudioUnit *audioUnit = NULL;
std::vector<float> convertedSampleBuffer;
OSStatus renderCallback(void                        *userData,
                        AudioUnitRenderActionFlags  *actionFlags,
                        const AudioTimeStamp        *audioTimeStamp,
                        UInt32                      busNumber,
                        UInt32                      numFrames,
                        AudioBufferList             *buffers) {
    
    OSStatus status = AudioUnitRender(*audioUnit,
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
    
    convertedSampleBuffer.reserve(numFrames);
    float * buf = convertedSampleBuffer.data();
    
    SInt16 *inputFrames = (SInt16*)(buffers->mBuffers->mData);
    
    // If your DSP code can use integers, then don't bother converting to
    // floats here, as it just wastes CPU. However, most DSP algorithms rely
    // on floating point, and this is especially true if you are porting a
    // VST/AU to iOS.
    for(int i = 0; i < numFrames; i++) {
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
    for (UInt32 i=0; i<buffers->mNumberBuffers; ++i)
        memset(buffers->mBuffers[i].mData, 0, buffers->mBuffers[i].mDataByteSize);

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

    (void) outputBuffer; /* Prevent unused variable warnings. */
    (void) timeInfo;
    (void) statusFlags;
    (void) userData;
    
    data->step((const SAMPLE*)inputBuffer, framesPerBuffer);
    
    return paContinue;
}
#endif
void paTestData::step(const SAMPLE *rptr, unsigned long framesPerBuffer)
{
    bool bFalse( false );
    while (!used.compare_exchange_strong(bFalse, true,
                                     std::memory_order_acquire,
                                     std::memory_order_relaxed))
    {}
    
    if(rptr)
    {
#if DBG_SAMPLES
        samplesSinceLastRead += framesPerBuffer;
#endif

        for( unsigned long i=0; i<framesPerBuffer; i++ )
        {
            auto val = *rptr++;
            algo_max.feed(val);
            
            // filter high frequencies
            auto filtered_value = avg.feed(val);
            algo_freq.feed(filtered_value);
        }
    }
    
    used.store(false, std::memory_order_release);
}


Audio::~Audio() {}
Audio * Audio::gInstance = NULL;
Audio& Audio::getInstance()
{
    if(!gInstance)
        gInstance = new Audio();
    return *gInstance;
}


#if TARGET_OS_IOS
int initAudioSession() {
    audioUnit = (AudioUnit*)malloc(sizeof(AudioUnit));
    
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

int initAudioStreams(AudioUnit *audioUnit, void * pData) {
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
    if(AudioComponentInstanceNew(component, audioUnit) != noErr) {
        return 1;
    }
    
    UInt32 enable = 1;
    if(AudioUnitSetProperty(*audioUnit, kAudioOutputUnitProperty_EnableIO,
                            kAudioUnitScope_Input, 1, &enable, sizeof(UInt32)) != noErr) {
        return 1;
    }
    
    AURenderCallbackStruct callbackStruct;
    callbackStruct.inputProc = renderCallback; // Render function
    callbackStruct.inputProcRefCon = pData;
    if(AudioUnitSetProperty(*audioUnit, kAudioUnitProperty_SetRenderCallback,
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
    if(AudioUnitSetProperty(*audioUnit, kAudioUnitProperty_StreamFormat,
                            kAudioUnitScope_Input, 0, &streamDescription, sizeof(streamDescription)) != noErr) {
        return 1;
    }
    
    // Ditto for the output stream, which we will be sending the processed audio to
    if(AudioUnitSetProperty(*audioUnit, kAudioUnitProperty_StreamFormat,
                            kAudioUnitScope_Output, 1, &streamDescription, sizeof(streamDescription)) != noErr) {
        return 1;
    }
    
    return 0;
}

int startAudioUnit(AudioUnit *audioUnit) {
    if(AudioUnitInitialize(*audioUnit) != noErr) {
        return 1;
    }
    
    if(AudioOutputUnitStart(*audioUnit) != noErr) {
        return 1;
    }
    
    return 0;
}

int stopProcessingAudio(AudioUnit *audioUnit) {
    if(AudioOutputUnitStop(*audioUnit) != noErr) {
        return 1;
    }
    
    if(AudioUnitUninitialize(*audioUnit) != noErr) {
        return 1;
    }
    
    *audioUnit = NULL;
    return 0;
}

#endif
void Audio::Init()
{
    if(bInitialized_)
    {
        LG(WARN, "Audio::Init already initialized");
        return;
    }

#if TARGET_OS_IOS
    if(0==initAudioSession())
    {
        bInitialized_ = true;
        
        if(0==initAudioStreams(audioUnit, &data))
        {
            if(0==startAudioUnit(audioUnit))
            {
            }
            else
            {
                LG(ERR, "startAudioUnit failed");
                A(0);
                return;
            }
        }
        else
        {
            LG(ERR, "initAudioStreams failed");
            A(0);
            return;
        }
    }
    else
    {
        LG(ERR, "initAudioSession failed");
        A(0);
        return;
    }
#else

    // set minimum latency env var to speed things up
    const char * lat = "PA_MIN_LATENCY_MSEC";
    const char * latVal = "1";
    int erri = setenv( lat, latVal, true);
    if(unlikely(erri))
    {
        LG(ERR, "Audio::get : Could not set env variable PA_MIN_LATENCY_MSEC: %d", errno);
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


    LG(INFO, "Audio::Init : initializing %s", Pa_GetVersionText());
    PaError err = Pa_Initialize();
    if(likely(err == paNoError))
    {
        LG(INFO, "Audio::Init : done initializing %s", Pa_GetVersionText());
        bInitialized_ = true;
        
        LG(INFO,"%d host apis", Pa_GetHostApiCount());
        
        PaStreamParameters inputParameters;
        inputParameters.device = Pa_GetDefaultInputDevice(); /* default input device */
        if (unlikely(inputParameters.device == paNoDevice)) {
            LG(ERR, "Audio::get : No default input device");
            A(0);
            return;
        }
        LG(INFO, "audio device : id %d", inputParameters.device);
        
        inputParameters.channelCount = NUM_CHANNELS;
        inputParameters.sampleFormat = PA_SAMPLE_TYPE;
        
        auto pi = Pa_GetDeviceInfo( inputParameters.device );
        LG(INFO, "audio device : hostApi    %d", pi->hostApi);
        LG(INFO, "audio device : name       %s", pi->name);
        LG(INFO, "audio device : maxIC      %d", pi->maxInputChannels);
        LG(INFO, "audio device : maxOC      %d", pi->maxOutputChannels);
        LG(INFO, "audio device : def. sr    %f", pi->defaultSampleRate);
        LG(INFO, "audio device : def. lilat %f", pi->defaultLowInputLatency);
        LG(INFO, "audio device : def. hilat %f", pi->defaultHighInputLatency);

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
            LG(ERR, "Pa_OpenStream failed : %s", Pa_GetErrorText(err));
            A(0);
            return;
        }
        
        const PaStreamInfo * si = Pa_GetStreamInfo(stream);
        
        LG(INFO, "stream : output lat  %f", si->outputLatency);
        LG(INFO, "stream : input lat   %f", si->inputLatency);
        LG(INFO, "stream : sample rate %f", si->sampleRate);
        
        err = Pa_StartStream( stream );
        if( unlikely(err != paNoError) )
        {
            LG(ERR, "Pa_StartStream failed : %s", Pa_GetErrorText(err));
            A(0);
            return;
        }
    }
    else
    {
        bInitialized_ = false;
        LG(ERR, "Audio::Init : PA_Initialize failed : %s", Pa_GetErrorText(err));
        A(0);
    }
#endif
}
void Audio::TearDown()
{
    if(likely(bInitialized_))
    {
#if TARGET_OS_IOS
        if(0==stopProcessingAudio(audioUnit))
        {
        }
        else{
            LG(ERR, "stopProcessingAudio failed");
            A(0);
        }
#else
        if(stream)
        {
            PaError err = Pa_CloseStream( stream );
            if( unlikely(err != paNoError) ) {
                LG(ERR, "Pa_CloseStream failed : %s", Pa_GetErrorText(err));
                A(0);
            }
        }
        
        PaError err = Pa_Terminate();
        if(unlikely(err != paNoError))
        {
            LG(ERR, "PA_Terminate failed : %s", Pa_GetErrorText(err));
        }
#endif
           
        bInitialized_ = false;
    }
    else
    {
        LG(ERR, "Audio::TearDown : was not initialized");
    }
}
bool AlgoMax::computeWhileLocked()
{
    copy = maxAbsSinceLastRead;
    maxAbsSinceLastRead = 0.f;
    return true;
}
bool AlgoMax::compute(float &f)
{
    f = copy;
    return true;
}
float Audio::getMaxAbs()
{
    return data.algo_max.getResult();
}

float Audio::getFrequency()
{
    return data.algo_freq.getResult();
}

/*
void FreqFromAutocorr::computeWhileLocked()
{
    processing.clear();
    processing.swap(willProcess);
}*/

void plot(const char*title,const std::vector<SAMPLE>&v)
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

bool FreqFromZC::compute(float & f)
{
    f = resultFreq_;
    return true;
}
bool FreqFromZC::computeWhileLocked()
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
        if(data.count == positive_zeros_dist.size() || delta > upperZero )
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
                resultFreq_ = candidate;
                return true;
            }
        }
    }
    
    return false;
}
