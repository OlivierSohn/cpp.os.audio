# define IMJ_PORTAUDIO_SAMPLE_TYPE  paFloat32
using SAMPLE = float;

typedef void PaStream; //from "portaudio.h"

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
    
    
    constexpr int SAMPLE_RATE = 44100;
    
    constexpr float int_period_to_freq(int period) {
        return SAMPLE_RATE / static_cast<float>(period);
    }
    
    constexpr int freq_to_int_period( float freq_hz ) {
        if(freq_hz <= 0.f) {
            return 1;
        }
        return (int) (((float)SAMPLE_RATE) / freq_hz);
    }
    
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
        using FPT = float;
        
        bool empty() const { return values.empty(); }
        auto size() const { return values.size(); }
        
        auto begin() const { return values.begin(); }
        auto end() const { return values.end(); }
        
        auto operator [] (int i) const { return values[i]; }
        
        soundBuffer(size_t n, float value) : values(n, value) {}
        
        soundBuffer( soundId const & );
        
    private:
        template < typename F >
        void generate( int period, F );

        auto begin() { return values.begin(); }
        auto end() { return values.end(); }

        cacheline_aligned_allocated::vector<FPT> values;
    };
    
    class Sounds {
        std::map< soundId, soundBuffer > sounds;
    public:
        soundBuffer const & get( soundId );
    };
    
}
