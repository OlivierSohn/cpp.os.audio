#include <stdlib.h> // pulls in declaration of malloc, free
#include <string.h>
#include <cerrno>
#include <algorithm>

#include "audio.h"
// SDL does not have audio recording yet so I use portaudio
//#include "SDL_audio.h"
#include "portaudio.h"
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

const int SAMPLE_RATE(44100);
const int FRAMES_PER_BUFFER(16);
const float NUM_SECONDS(0.001f);
const int NUM_CHANNELS(1);
/* #define DITHER_FLAG     (paDitherOff)  */
#define DITHER_FLAG     (0) /**/

#define CHECK_OVERFLOW  (0)
#define CHECK_UNDERFLOW  (0)



/* This routine will be called by the PortAudio engine when audio is needed.
 ** It may be called at interrupt level on some machines so don't do anything
 ** that could mess up the system like calling malloc() or free().
 */
static int recordCallback( const void *inputBuffer, void *outputBuffer,
                          unsigned long framesPerBuffer,
                          const PaStreamCallbackTimeInfo* timeInfo,
                          PaStreamCallbackFlags statusFlags,
                          void *userData )
{
    paTestData *data = (paTestData*)userData;
    const SAMPLE *rptr = (const SAMPLE*)inputBuffer;
    SAMPLE *wptr = &data->recordedSamples[/*data->frameIndex * NUM_CHANNELS*/0];
    data->lastWriteSampleCount = framesPerBuffer;

    if(framesPerBuffer > data->maxFrameIndex)
    {
        data->lastWriteSampleCount = data->maxFrameIndex;
        rptr += framesPerBuffer - data->maxFrameIndex; // to write the end of the buffer
    }
    long framesToCalc(data->lastWriteSampleCount);
    //unsigned long framesLeft = data->maxFrameIndex - data->frameIndex;
    
    (void) outputBuffer; /* Prevent unused variable warnings. */
    (void) timeInfo;
    (void) statusFlags;
    (void) userData;
    
    static int c = 0;
    
    bool bFalse( false );
    if (data->used.compare_exchange_strong(bFalse, true,
                                           std::memory_order_acquire,
                                           std::memory_order_relaxed))
    {
        c++;
        // The lock succeeded
        
        if( inputBuffer == NULL )
        {
            for( long i=0; i<framesToCalc; i++ )
            {
                *wptr++ = SAMPLE_SILENCE;  /* left */
                if( NUM_CHANNELS == 2 ) *wptr++ = SAMPLE_SILENCE;  /* right */
            }
        }
        else
        {
            for( long i=0; i<framesToCalc; i++ )
            {
                *wptr++ = *rptr++;  /* left */
                if( NUM_CHANNELS == 2 ) *wptr++ = *rptr++;  /* right */
            }
        }
        
        data->used.store(false, std::memory_order_release);
    }
    else
    {
        LG(INFO,"failed after %d", c);
        c=0;
    }
    
    
    return paContinue;
}


Audio::Audio() {}
Audio::~Audio() {}
Audio * Audio::gInstance = NULL;
Audio& Audio::getInstance()
{
    if(!gInstance)
        gInstance = new Audio();
    return *gInstance;
}
void Audio::Init()
{
    if(bInitialized_)
    {
        LG(WARN, "Audio::Init already initialized");
        return;
    }

    // set minimum latency env var to speed things up
    const char * lat = "PA_MIN_LATENCY_MSEC";
    const char * latVal = "1";
    int erri = setenv( lat, latVal, true);
    if(erri)
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
    if(err == paNoError)
    {
        LG(INFO, "Audio::Init : done initializing %s", Pa_GetVersionText());
        bInitialized_ = true;
        
        LG(INFO,"%d host apis", Pa_GetHostApiCount());
        
        data.maxFrameIndex = (unsigned long) (NUM_SECONDS * SAMPLE_RATE); /* Record for a few seconds. */
        data.numSamples = data.maxFrameIndex * NUM_CHANNELS;
        
        if(data.recordedSamples)
            free (data.recordedSamples);
        data.recordedSamples = (SAMPLE *) malloc( data.numSamples * sizeof(SAMPLE) );
    
        if( data.recordedSamples == NULL )
        {
            LG(ERR, "Audio::get : Could not allocate record array");
            A(0);
            return;
        }
        for( auto i=0; i<data.numSamples; i++ ) data.recordedSamples[i] = 0;
        
        PaStreamParameters inputParameters;
        inputParameters.device = Pa_GetDefaultInputDevice(); /* default input device */
        if (inputParameters.device == paNoDevice) {
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

        inputParameters.suggestedLatency = 0.000001/*pi->defaultLowInputLatency*/;
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
        if( err != paNoError )
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
        if( err != paNoError )
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
}
void Audio::TearDown()
{
    if(bInitialized_)
    {
        if(stream)
        {
            PaError err = Pa_CloseStream( stream );
            if( err != paNoError ) {
                LG(ERR, "Pa_CloseStream failed : %s", Pa_GetErrorText(err));
                A(0);
            }
        }
        
        bInitialized_ = false;
        
        PaError err = Pa_Terminate();
        if(err != paNoError)
        {
            LG(ERR, "PA_Terminate failed : %s", Pa_GetErrorText(err));
        }
    }
    else
    {
        LG(ERR, "Audio::TearDown : was not initialized");
    }
}
float Audio::get()
{
    // TODO compute only once per modeltime
    if(stream)
    {
        SAMPLE max(0), average(0);

        // critical section
        while (data.used.exchange(true)) { }
        
        for(auto i = 0; i< data.lastWriteSampleCount; i++)
        {
            SAMPLE val = data.recordedSamples[i];
            max = std::max( val, max);
            if(i==0)
                average = val;
            else
                average += val;
        }

        data.used = false; // unlock

        average /= data.lastWriteSampleCount;

        return average;
    }
    
    return 0.f;
}


