

namespace imajuscule {
    class Audio;
    namespace Sensor {
        
        constexpr static const float minFreq = 80.f; // in herz (based on my voice :) )
        constexpr static const float maxFreq = 440.f; // in herz (also based on my voice :) )
        // and we want to have at least 3 samplings per period of max frequency, so:
        constexpr static const float samples_per_max_freq_period = 8.f;
        constexpr static const float time_between_representative_samples = 1.f / (samples_per_max_freq_period * maxFreq);
        constexpr static const int sampling_period = (int)(((float)SAMPLE_RATE) * time_between_representative_samples + 0.5f);
        
        
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
                // dont reset resultFreq_ !
                
                signal_range.set(0.f,0.f);
                positive_zeros_dist.reset();
                acc = 0;
                bWasNeg = true;
                counter = sampling_period;
            }
            
            FreqFromZC(std::atomic_bool &a)
            : Sensor<FreqFromZC, NO_LOCK, float>(&a)
            , positive_zeros_dist(16, 0)
            , signal_range(0.f,0.f)
            , bWasNeg(true)
            {
                filter_.initWithSampleRate(((float)SAMPLE_RATE)/(float)sampling_period,
                                           50);
            }
            
            void feed(SAMPLE val)
            {
                counter++;
                if(counter < sampling_period) {
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
            int32_t counter = sampling_period;
            Filter<float, 1, FilterType::HIGH_PASS> filter_;
            
            cyclic<int> positive_zeros_dist; // zero crossing intervals are recorded over several time steps
            int32_t acc = 0;
            bool bWasNeg : 1;
            
            range<float> signal_range; // range is representative of a single time step (except for very first calculation of a series)
            
            std::string const name = std::string("AUF");
        };
        
        
        // declared NO_LOCK to share Lock between multiple algorithms
        struct AlgoMax : public Sensor<AlgoMax, NO_LOCK, float>
        {
            friend class Sensor<AlgoMax, NO_LOCK, float>;
            
            std::string const & getVarName() { return name; }
            const char * getVarDoc() { return "Audio amplitude"; }
            
            AlgoMax(std::atomic_bool &a)
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
            
            paTestData( Activator & a ) :
            algo_freq(used)
            , algo_max(used)
            , avg(sizeSlidingAverage)
            , activator(a)
            {}
            
            void step(const SAMPLE * inputBuffer, int nFrames);
            
            FreqFromZC algo_freq;
            
            AlgoMax algo_max;
            
#if TARGET_OS_IOS
            std::vector<float> convertedSampleBuffer;
#endif
            
        private:
            Activator & activator;
            slidingAverage<float> avg;
            
            std::atomic_bool used { false };
        };
        
        class AudioIn : public Activator
        {
            friend class imajuscule::Audio;
        public:
            void Init();
            void TearDown();
            bool Initialized() const { return bInitialized_; }
        protected:
            bool do_wakeup() override;
            bool do_sleep() override;
        private:
            static constexpr auto AUDIO_UNUSED_FRAME_COUNT_FOR_SLEEP = 689; // is 1s. when 64 frames / callback
            AudioIn() : Activator ( AUDIO_UNUSED_FRAME_COUNT_FOR_SLEEP ),
            data( *this ),
            bInitialized_(false)
            {};
            
            bool bInitialized_ : 1;
            PaStream *stream = nullptr;
            
            paTestData data;
        };
    }
    
    class AudioOut : public NonCopyable {
        
        static constexpr auto xfade_on_close = 200;
        static constexpr auto n_max_orchestrators_per_channel = 1;
        
        // members
        
        PaStream *stream = nullptr;
        bool bInitialized : 1;
        bool closing : 1;
        outputData data{
            std::numeric_limits<uint8_t>::max(),
            n_max_orchestrators_per_channel
        };
        Sounds sounds;

        // methods
    private:
        friend class Audio;

        AudioOut() : bInitialized(false), closing(false) {}
        ~AudioOut() {
            data.closeAllChannels(0); // needs to be called before 'Sounds' destructor
        }
        
        void Init();
        void TearDown();

    public:
        using Volumes = decltype(data)::Volumes;
        static constexpr auto nAudioOut = decltype(data)::nOuts;
        using Request = decltype(data)::Request;

        auto & getChannelHandler() { return data; }
        void onApplicationShouldClose() {
            if(closing) {
                return;
            }
            closing = true;
            data.closeAllChannels(xfade_on_close);
            LG(INFO, "Fading out Audio before shutdown...");
        }
        
        bool Initialized() const { return bInitialized; }
        uint8_t openChannel(float volume = 1.f,
                            ChannelClosingPolicy p = ChannelClosingPolicy::ExplicitClose,
                            int xfade_length = 401) {
            if(closing) {
                return AUDIO_CHANNEL_NONE;
            }
            Init();
            return data.template openChannel<WithLock::Yes>(volume, p, xfade_length);
        }

        void play( uint8_t channel_id, StackVector<Request> && v ) {
            if(closing) {
                return;
            }
            data.play( channel_id, std::move( v ) );
        }
        
        template<class ...Args>
        void playGeneric( uint8_t channel_id, Args&& ...args ) {
            if(closing) {
                return;
            }
            data.playGeneric( channel_id, std::forward<Args>( args )... );
        }
        
        void setVolume( uint8_t channel_id, float volume ) {
            if(closing) {
                return;
            }
            data.setVolume( channel_id, volume);
        }

        void closeChannel(uint8_t channel_id, CloseMode mode) {
            if(closing) {
                return;
            }
            data.closeChannel( channel_id, mode );
        }
        
        int get_xfade_millis(uint8_t channel_id) const {
            return data.getChannel(channel_id).duration_millis_xfade();
        }
        
        Sounds & editSounds() { return sounds; }
    };

    class Audio {
        friend class Globals;
    public:
        static constexpr auto nAudioOut = AudioOut::nAudioOut;
        using Request = typename AudioOut::Request;

        static void Init();
        static void TearDown();
        static Audio * getInstance();
        
        AudioOut & out() { return audioOut; }
        
    private:
        Audio() = default;
        ~Audio() = default;
        static Audio * gInstance;
        Sensor::AudioIn audioIn;
        AudioOut audioOut;
        
        void doInit();
        void doTearDown();
    };
}
