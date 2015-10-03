#pragma once
#include <atomic>
#include <algorithm>
#include <numeric>
#include "os.log.h"
#include "math.h"
#include "cg.math.numeric.h"
#include "cg.math.filter.h"

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

#include <vector>

const int SAMPLE_RATE(44100);

namespace imajuscule
{
    enum class Result { NOT_ENOUGH_DATA, NOT_ENOUGH_MEMORY, OK, PREVIOUS_VALUE };

    constexpr static const float minFreq = 80.f; // in herz (based on my voice :) )
    constexpr static const float maxFreq = 440.f; // in herz (also based on my voice :) )
    // and we want to have at least 3 samplings per period of max frequency, so:
    constexpr static const float samples_per_max_freq_period = 8.f;
    constexpr static const float time_between_representative_samples = 1.f / (samples_per_max_freq_period * maxFreq);
    constexpr static const int sampling_period = SAMPLE_RATE * time_between_representative_samples;

    struct FreqAlgo
    {
        virtual bool needsLock() = 0;
        // if needsLock returns true, this is called in the main thread, while lock is taken
        virtual void computeWhileLocked(){};
        // called in the main thread, outside lock scope
        virtual Result computeFrequency(float & f) = 0;

    };
    
    enum CyclicInitializationType
    {
        OVERWRITE_INITIAL_VALUES_WITH_FIRST_FEED,
        KEEP_INITIAL_VALUES
    };
    
    template<class T, int KIND = KEEP_INITIAL_VALUES>
    struct cyclic
    {
        using container = typename std::vector<T>;
        using iterator = typename container::iterator;
        using const_iterator = typename container::const_iterator;
        
        operator container & () { return buf; }
        operator container const& () const { return buf; }
        
        const_iterator begin() const { return buf.begin();}
        iterator begin() { return buf.begin();}
        const_iterator cycleEnd() const { return it;}
        iterator cycleEnd() { return it;}
        const_iterator end() const { return buf.end();}
        iterator end() { return buf.end();}

        size_t size() const { return buf.size(); }
        
        cyclic(size_t size, T initVals)
        :initialValue(initVals)
        {
            buf.resize(size, initVals);
            it = buf.begin();
        }
        void feed(T val)
        {
            *it = val;
            ++it;
            if(it == buf.end())
                it = buf.begin();
            
            if(isFirstFeed)
            {
                if(KIND == OVERWRITE_INITIAL_VALUES_WITH_FIRST_FEED)
                {
                    std::fill(buf.begin(), buf.end(), val);
                }
                isFirstFeed = false;
            }
        }
        void reset()
        {
            std::fill(buf.begin(), buf.end(), initialValue);
            it = buf.begin();
            isFirstFeed = true;
        }
    private:
        container buf;
        iterator it;
        T initialValue;
        bool isFirstFeed = true;
    };
    
    struct slidingAverage
    {
        slidingAverage(size_t size) :
        values_(size, 0.f)
        {
        }
        float feed(float val)
        {
            values_.feed(val);
            return get();
        }
        float get() const
        {
            return std::accumulate(values_.begin(), values_.end(), 0.f) / (float) values_.size();
        }
        void reset()
        {
            values_.reset();
        }
    private:
        cyclic<float, OVERWRITE_INITIAL_VALUES_WITH_FIRST_FEED> values_;
    };
    template <class T>
    struct range
    {
        range(){min_ = 10; max_ = -10;} // to force assert if not set afterwards
        range(T min, T max):
        min_(min)
        , max_(max)
        {
            A(min <= max);
        }
        T delta() const
        {
            T res = max_ - min_;
            return res;
        }
        void set(T min, T max)
        {
            A(min <= max);
            min_ = min;
            max_ = max;
        }
        bool extend(T val)
        {
            if(val > max_)
            {
                max_ = val;
                return true;
            }
            else if(val < min_)
            {
                min_ = val;
                return true;
            }
            return false;
        }
        bool contains(T val) const
        {A(max_>=min_);
            return (val <= max_ && val >= min_);
        }
        T getMax() const {return max_;}
        T getMin() const {return min_;}
    private:
        T min_, max_;
    };
    struct FreqFromZC : public FreqAlgo
    {
        // use constant 0.01 instead of 0 to not record zero crossing related to noise
        const SAMPLE upperZero = 0.01f;
        
        bool needsLock () override {return true;}
        void computeWhileLocked() override;
        
        void reset()
        {
            // dont reset previousFreq !

            signal_range.set(0.f,0.f);
            positive_zeros_dist.reset();
            acc = 0;
            bWasNeg = true;
            counter = sampling_period;
        }
        
        FreqFromZC()
        : positive_zeros_dist(16, 0)
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
        
        Result computeFrequency(float & f) override;
        
    private:
        int counter = sampling_period;
        Filter<float, 1, FilterType::HIGH_PASS> filter_;
        
        cyclic<int> positive_zeros_dist; // zero crossing intervals are recorded over several time steps
        int acc = 0;
        bool bWasNeg = true;
        
        float previousFreq = 0.f;
        range<float> signal_range; // range is representative of a single time step (except for very first calculation of a series)
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
    
    struct paTestData
    {
        const size_t sizeSlidingAverage = 160;
        
        paTestData()
        : avg(sizeSlidingAverage)
        {}
        
        void step(const SAMPLE * inputBuffer, unsigned long framesPerBuffer);
        
        //FreqFromPeaks
        //FreqFromAutocorr
        FreqFromZC
            algo_freq;
        SAMPLE maxAbsSinceLastRead = 0.f;
        slidingAverage avg;

        std::atomic_bool used { false };
    };
    
    class Audio
    {
    public:
        static Audio & getInstance();
        void Init();
        void TearDown();
        float getMaxAbs(float guiTime);
        Result getFrequency(float guiTime, float & f);
        ~Audio();
    private:
        Audio() {};
        static Audio * gInstance;
        bool bInitialized_ = false;
        typedef void PaStream; //#include "portaudio.h"
        PaStream *stream = NULL;
        
        paTestData data;
        
        struct
        {
            bool hasResultForTime(float t) const
            {
                return (computedOnce_ && timeComputation_ == t);
            }
            float result() const
            {
                A(computedOnce_);
                return value_;
            }
            void storeResultForTime(float res, float t)
            {
                computedOnce_ = true;
                timeComputation_ = t;
                value_ = res;
            }
        private:
            bool computedOnce_ = false;
            float timeComputation_;
            float value_;
        } maxAbs_result, freq_result;
    };
}