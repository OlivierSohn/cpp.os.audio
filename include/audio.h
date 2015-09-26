#pragma once
#include <atomic>

/* Select sample format. */
#if 1
#define PA_SAMPLE_TYPE  paFloat32
typedef float SAMPLE;
#define SAMPLE_SILENCE  (0.0f)
#define PRINTF_S_FORMAT "%.8f"
#elif 0
#define PA_SAMPLE_TYPE  paInt16
typedef short SAMPLE;
#define SAMPLE_SILENCE  (0)
#define PRINTF_S_FORMAT "%d"
#elif 0
#define PA_SAMPLE_TYPE  paInt8
typedef char SAMPLE;
#define SAMPLE_SILENCE  (0)
#define PRINTF_S_FORMAT "%d"
#else
#define PA_SAMPLE_TYPE  paUInt8
#define SAMPLE_SILENCE  (128)
#define PRINTF_S_FORMAT "%d"
#endif

namespace imajuscule
{
    struct paTestData
    {
        unsigned long lastWriteSampleCount = 0;
        unsigned long maxFrameIndex = 0;
        unsigned long numSamples = 0;
        SAMPLE      *recordedSamples = NULL;
        std::atomic_bool used { false };
    };
    
    class Audio
    {
    public:
        static Audio & getInstance();
        void Init();
        void TearDown();
        float get();
        ~Audio();
    private:
        Audio();
        static Audio * gInstance;
        bool bInitialized_ = false;
        typedef void PaStream; //#include "portaudio.h"
        PaStream *stream = NULL;
        
        paTestData data;
        bool bComputedOnce = false;
        float timeComutation;
        float value;
    };
}