

namespace imajuscule {

class Audio;

namespace sensor {

constexpr static const float minFreq = 80.f; // in herz (based on my voice :) )
constexpr static const float maxFreq = 440.f; // in herz (also based on my voice :) )
// and we want to have at least 3 samplings per period of max frequency, so:
constexpr static const float samples_per_max_freq_period = 8.f;
constexpr static const float time_between_representative_samples = 1.f / (samples_per_max_freq_period * maxFreq);
constexpr static const int sampling_period(int sample_rate) {
  return (int)(((float)sample_rate) * time_between_representative_samples + 0.5f);
}


// declared NO_LOCK to share Lock between multiple algorithms
struct FreqFromZC : public Sensor<FreqFromZC, NO_LOCK, float>
{
  friend class Sensor<FreqFromZC, NO_LOCK, float>;
  
  std::string const & getVarName() { return name; }
  const char * getVarDoc() { return "Audio frequency"; }
  
  // use constant epsilon instead of 0 to not record zero crossing related to noise
  static constexpr SAMPLE upperZero =
#ifdef _WIN32
  0.005f // TODO make that user configurable (ask user to record noise)
#else
  0.01f
#endif
  ;
  
  InternalResult computeWhileLocked(float & f);
  
  void reset()
  {
    // don't reset resultFreq_ !
    
    signal_range.set(0.f,0.f);
    positive_zeros_dist.reset();
    acc = 0;
    bWasNeg = true;
    counter = sampling_period(sample_rate);
  }
  
  FreqFromZC(int sampleRate, std::atomic_flag &a)
  : Sensor<FreqFromZC, NO_LOCK, float>(&a)
  , positive_zeros_dist(16)
  , signal_range(0.f,0.f)
  , bWasNeg(true)
  , sample_rate(sampleRate)
  , counter(sampling_period(sampleRate))
  , sampling_period_(sampling_period(sampleRate))
  {
    filter_.initWithFreq(1.f/time_between_representative_samples,
                         50.f/(2.f*M_PI));
  }
  
  void feed(SAMPLE val)
  {
    counter++;
    if (counter < sampling_period_) {
      return;
    }
    counter = 0;
    
    // high pass
    filter_.feed(&val);
    val = *filter_.filtered();
    
    signal_range.extend(val);
    
    if(val > upperZero && bWasNeg) {
      positive_zeros_dist.feed(acc);
      acc = 0;
      bWasNeg = false;
    }
    else if(!bWasNeg && val< upperZero) {
      bWasNeg = true;
    }
    acc++;
  }
  
  InternalResult compute(float & f);
  
private:
  int32_t counter;
  int32_t sampling_period_;
  audio::Filter<float, 1, audio::FilterType::HIGH_PASS> filter_;
  
  cyclic<int> positive_zeros_dist; // zero crossing intervals are recorded over several time steps
  int32_t acc = 0;
  bool bWasNeg : 1;
  
  range<float> signal_range; // range is representative of a single time step (except for very first calculation of a series)
  int sample_rate;
  
  std::string const name = std::string("AUF");
};


// declared NO_LOCK to share Lock between multiple algorithms
struct AlgoMax : public Sensor<AlgoMax, NO_LOCK, float>
{
  friend class Sensor<AlgoMax, NO_LOCK, float>;
  
  std::string const & getVarName() { return name; }
  const char * getVarDoc() { return "Audio amplitude"; }
  
  AlgoMax(std::atomic_flag &a)
  : Sensor<AlgoMax, NO_LOCK, float>(&a)
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
  static constexpr auto sizeSlidingAverage = 160;
  
  paTestData( int sample_rate, Activator & a ) :
  algo_freq(sample_rate, used)
  , algo_max(used)
  , avg(sizeSlidingAverage)
  , activator(a)
  {
  }
  
  void step(const SAMPLE * inputBuffer, int nFrames);
  
  FreqFromZC algo_freq;
  
  AlgoMax algo_max;
    
private:
  Activator & activator;
  slidingAverage<float> avg;
  
  std::atomic_flag used = ATOMIC_FLAG_INIT;
};

class AudioIn : public Activator
{
  friend class imajuscule::Audio;
  
  static constexpr auto AudioPlat =
#if TARGET_OS_IOS
  audio::AudioPlatform::AudioUnits;
#else
  audio::AudioPlatform::PortAudio;
#endif

public:
  bool Init();
  void TearDown();
  bool Initialized() const { return bInitialized_; }
protected:
  bool do_wakeup() override;
  bool do_sleep() override;
private:
  static constexpr auto AUDIO_UNUSED_FRAME_COUNT_FOR_SLEEP = 689; // is 1s. when 64 frames / callback

  AudioIn(int sampleRate, float minLatency)
  : Activator (AUDIO_UNUSED_FRAME_COUNT_FOR_SLEEP)
  , data( sampleRate, *this )
  , bInitialized_(false)
  , sample_rate(sampleRate)
  , min_latency(minLatency)
  {}
  
  bool bInitialized_ : 1;
  audio::AudioInput<AudioPlat> audio_input;
  int sample_rate;
  double min_latency;
  paTestData data;
};

} // NS sensor
} // NS Imajuscule
