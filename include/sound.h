
namespace imajuscule {

    struct Sound {
        enum Type : unsigned char {
            SINE,
            TRIANGLE,
            SAW,
            SQUARE,
            NOISE,
            SILENCE,
            ONE
        } type : 3;
        
        static constexpr auto ConstantSoundDummyFrequency = 1.f;
        
        constexpr bool zeroOnPeriodBoundaries() const { return type == SINE || type == TRIANGLE; }
        constexpr bool operator == (const Sound & other) const { return type == other.type; }
        constexpr bool operator < (const Sound & other) const { return type < other.type; }
        Sound(Type t) : type(t) {}
        Sound() = default;

        constexpr int minimalPeriod() const {
            switch(type) {
                case SINE:
                    return 3;
                case SQUARE:
                    return 3;
                case TRIANGLE:
                    return 2;
                case SAW:
                    return 3; // 2 would give the same result as a triangle
                case NOISE:
                    return 1;
                case SILENCE:
                    return 0;
                case ONE:
                    return 0;
            }
            return 1;
        }
    };
    
    struct soundId {
        soundId() = default;
        
        soundId( Sound sound, float freq_hz = 1.f )
        :
        sound(sound),
        period_length( (sound == Sound::SILENCE) ? 1 : freq_to_int_period( freq_hz ) )
        {}
        
        Sound sound;
        int32_t period_length;
        bool operator < (const soundId & other) const {
            if(sound == other.sound) {
                return ( period_length < other.period_length );
            }
            return( sound < other.sound );
        }
    };
    
    struct soundBuffer {
        using value_type = float;
        using FPT = value_type;
        
        using buffer = cacheline_aligned_allocated::vector<value_type>;
        
        // no copy
        soundBuffer(const soundBuffer &) = delete;
        soundBuffer & operator=(const soundBuffer&) = delete;

        soundBuffer(soundBuffer &&) = default;
        soundBuffer& operator = (soundBuffer &&) = default;
        
        bool empty() const { return values.empty(); }
        auto size() const { return values.size(); }
        
        auto begin() const { return values.begin(); }
        auto end() const { return values.end(); }
        
        auto operator [] (int i) const { return values[i]; }
        
        soundBuffer(size_t n, float value) : values(n, value) {}
        
        soundBuffer( soundId const & );
        
        auto & getBuffer() { return values; }
        
    private:
        template < typename F >
        void generate( int period, F );

        auto begin() { return values.begin(); }
        auto end() { return values.end(); }

        buffer values;
    };
    
    template<typename T, size_t N>
    size_t countActiveAudioElements(std::array<T, N> & aes) {
        return std::count_if(aes.begin(), aes.end(), [](T const & elt){ return elt.isActive(); });
    }
    template<typename T, size_t N>
    T * editInactiveAudioElement(std::array<T, N> & aes) {
        auto it = std::find_if(aes.begin(), aes.end(), [](T const & elt){ return elt.isInactive(); });
        return (it == aes.end()) ? nullptr : &*it;
    }
    
    class Sounds {
        std::map< soundId, soundBuffer > sounds;
        std::array<audioelement::Square<float>, 8> squares;
        std::array<audioelement::Oscillator<float>, 8> oscillators;
        std::array<audioelement::FreqRamp<float>, 6> ramps;
 
        std::array<audioelement::RingModulation<
        audioelement::LowPassAlgo<audioelement::PulseTrainAlgo<float>>,
        audioelement::OscillatorAlgo<float>
        >, 6> ringmods;
        
        std::array<audioelement::LowPass<audioelement::PulseTrainAlgo<float>>, 6> lptrains;
    public:
        soundBuffer & get( soundId );
        
        audioelement::Oscillator<float> * getInactiveOscillator() {
            return editInactiveAudioElement(oscillators);
        }
        
        audioelement::Square<float> * getInactiveSquare() {
            return editInactiveAudioElement(squares);
        }
        
        audioelement::FreqRamp<float> * getInactiveFreqRamp() {
            return editInactiveAudioElement(ramps);
        }
        
        auto * getInactiveRingMod() {
            return editInactiveAudioElement(ringmods);
        }
        
        audioelement::LowPass<audioelement::PulseTrainAlgo<float>> * getInactiveLPTrain() {
            return editInactiveAudioElement(lptrains);
        }

        
        size_t countActiveOscillators() {
            return countActiveAudioElements(oscillators);
        }
        size_t countActiveSquares() {
            return countActiveAudioElements(squares);
        }
        size_t countActiveFreqRamps() {
            return countActiveAudioElements(ramps);
        }
        size_t countActiveRingMods() {
            return countActiveAudioElements(ringmods);
        }
        size_t countActiveLPTrains() {
            return countActiveAudioElements(lptrains);
        }
    };
    
}
