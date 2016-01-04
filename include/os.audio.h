#pragma once

#include <atomic>

#include "os.log.h"
#include "cg.math.numeric.h"
#include "cg.math.filter.h"
#include "cg.math.sensors.h"

#define DBG_SAMPLES 0

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

const int SAMPLE_RATE(44100);

namespace imajuscule
{
    constexpr static const float minFreq = 80.f; // in herz (based on my voice :) )
    constexpr static const float maxFreq = 440.f; // in herz (also based on my voice :) )
    // and we want to have at least 3 samplings per period of max frequency, so:
    constexpr static const float samples_per_max_freq_period = 8.f;
    constexpr static const float time_between_representative_samples = 1.f / (samples_per_max_freq_period * maxFreq);
    constexpr static const int sampling_period = (int)(((float)SAMPLE_RATE) * time_between_representative_samples + 0.5f);

    
    struct FreqFromZC : public TimedCompute<FreqFromZC, NO_LOCK, float>
    {
        friend class TimedCompute<FreqFromZC, NO_LOCK, float>;
        
        std::string const & getVarName() { return name; }

        // use constant epsilon instead of 0 to not record zero crossing related to noise
        const SAMPLE upperZero =
#ifdef _WIN32
            0.005f // TODO make that user configurable (ask user to record noise)
#else
            0.01f
#endif
            ;
        
        InternalResult computeWhileLocked(float & f);
        
        void reset()
        {
            // dont reset resultFreq_ !

            signal_range.set(0.f,0.f);
            positive_zeros_dist.reset();
            acc = 0;
            bWasNeg = true;
            counter = sampling_period;
        }
        
        FreqFromZC(std::atomic_bool &a)
        : TimedCompute<FreqFromZC, NO_LOCK, float>(&a)
        , positive_zeros_dist(16, 0)
        , signal_range(0.f,0.f)
        {
            filter_.initWithSampleRate(((float)SAMPLE_RATE)/(float)sampling_period, 50, false);
        }
        
        void feed(SAMPLE val)
        {
            counter++;
            if(counter < sampling_period)
                return;
            counter = 0;
            
            // high pass
            filter_.feed(&val);
            val = *filter_.filtered();
            
            signal_range.extend(val);
            
            if(val > upperZero && bWasNeg)
            {
                positive_zeros_dist.feed(acc);
                acc = 0;
                bWasNeg = false;
            }
            else
            {
                if(!bWasNeg && val< upperZero)
                    bWasNeg = true;
            }

            acc++;
        }
        
        InternalResult compute(float & f);
        
    private:
        int counter = sampling_period;
        Filter<float, 1, FilterType::HIGH_PASS> filter_;
        
        cyclic<int> positive_zeros_dist; // zero crossing intervals are recorded over several time steps
        int acc = 0;
        bool bWasNeg = true;
        
        range<float> signal_range; // range is representative of a single time step (except for very first calculation of a series)

        std::string name = std::string("AUF");
    };
    
    /*
    // according to http://www.fon.hum.uva.nl/paul/papers/Proceedings_1993.pdf
    struct FreqFromAutocorr : public FreqAlgo
    {
        bool needsLock () override {return true;}

        static const size_t bufferLength = 2048; // TODO adapt wrt lowest freq, so that hanning window spans over 3 periods
        static const size_t bufferWithPaddingLength = ceil_power_of_two(bufferLength + bufferLength/2);
        FreqFromAutocorr()
        {
            fft1.resize(bufferWithPaddingLength);
            withZeroPading.resize(bufferWithPaddingLength);
            processing.reserve(bufferLength);
            willProcess.reserve(bufferLength);
            live.reserve(bufferLength);
            hanning.reserve(bufferLength);
            inv_hanning_autocorr.reserve(bufferLength);
            for(int i=0; i<bufferLength; ++i)
            {
                float fi = ((float)i) / (float)bufferLength;
                float coeff = 2.f*M_PI * fi ;
                float c = cos(coeff);
                
                hanning.push_back( 0.5f - (0.5f * c) );
                float s =sin(coeff);
                
                inv_hanning_autocorr.push_back( 1.f /
                                               ( (1.f - fi)*(2.f/3.f + c/3.f) + s/(2.f*M_PI))
                                               );
            }
            
        }
        ~FreqFromAutocorr();
        
        // called in the audio thread
        void feed(SAMPLE val)
        {
            live.push_back(val);
            if(live.size()==bufferLength)
            {
                willProcess.swap(live);
                live.clear();
            }
        }
        
        // called in the main thread, while lock is taken
        void computeWhileLocked() override;
        // called in the main thread, outside lock scope
        Result computeFrequency(float & f) override;
    private:
        // when live is full, it is swapped with willProcess.
        // when computeFrequency is called, processing and willProcess are swapped
        std::vector<SAMPLE> withZeroPading, processing, hanning, inv_hanning_autocorr, willProcess, live;
        
        typedef struct { // Ugly, to not have to include kissfft header here
            SAMPLE r;
            SAMPLE i;
        }my_kiss_fft_cpx;
        std::vector<my_kiss_fft_cpx> fft1;
        
        void * cfg = NULL;
        void * cfg2 = NULL;
        // in the main thread
        float mean() const
        {
            SAMPLE mean(0.f);
            for(auto i : processing)
                mean += i;
            return mean / (SAMPLE) bufferLength;
        }
    };
    */
    
    struct AlgoMax : public TimedCompute<AlgoMax, NO_LOCK, float>
    {
        friend class TimedCompute<AlgoMax, NO_LOCK, float>;
        
        std::string const & getVarName() { return name; }
        
        AlgoMax(std::atomic_bool &a)
        : TimedCompute<AlgoMax, NO_LOCK, float>(&a)
        {}
        InternalResult computeWhileLocked(float & f);
        
        void feed(SAMPLE val)
        {
            val = std::abs(val);
            maxAbsSinceLastRead = std::max(maxAbsSinceLastRead, val);
        }
        
        InternalResult compute(float & f);
        
    private:
        SAMPLE maxAbsSinceLastRead = 0.f;
        std::string name = std::string("AU");
    };
    

    struct paTestData
    {
        const size_t sizeSlidingAverage = 160;
        
        paTestData()
        : algo_freq(used)
        , algo_max(used)
        , avg(sizeSlidingAverage)
        {}
        
        void step(const SAMPLE * inputBuffer, unsigned long framesPerBuffer);
        
        //FreqFromPeaks
        //FreqFromAutocorr
        FreqFromZC
            algo_freq;
        
        AlgoMax algo_max;
        
#if DBG_SAMPLES
        unsigned int samplesSinceLastRead = 0;
#endif

        slidingAverage avg;

        std::atomic_bool used { false };
    };
    
    class Audio
    {
    public:
        static Audio & getInstance();
        void Init();
        void TearDown();
        float getMaxAbs();
        float getFrequency();
        ~Audio();
    private:
        Audio() {};
        static Audio * gInstance;
        bool bInitialized_ = false;
        typedef void PaStream; //#include "portaudio.h"
        PaStream *stream = NULL;
        
        paTestData data;
    };
}
