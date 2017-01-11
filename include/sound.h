#if 1
# define PA_SAMPLE_TYPE  paFloat32
using SAMPLE = float;
# define SAMPLE_SILENCE  (0.0f)
# define PRINTF_S_FORMAT "%.8f"
#elif 0
# define PA_SAMPLE_TYPE  paInt16
using SAMPLE = short;
# define SAMPLE_SILENCE  (0)
# define PRINTF_S_FORMAT "%d"
#elif 0
# define PA_SAMPLE_TYPE  paInt8
using SAMPLE = char;
# define SAMPLE_SILENCE  (0)
# define PRINTF_S_FORMAT "%d"
#else
# define PA_SAMPLE_TYPE  paUInt8
# define SAMPLE_SILENCE  (128)
# define PRINTF_S_FORMAT "%d"
#endif

typedef void PaStream; //from "portaudio.h"

namespace imajuscule {
    
    struct Sound {
        bool zeroOnPeriodBoundaries() const { return type == SINE || type == TRIANGLE; }
        enum Type : unsigned char {SINE, TRIANGLE, SAW, SQUARE, NOISE, SILENCE, ONE } type : 3;
        bool operator == (const Sound & other) const { return type == other.type; }
        bool operator < (const Sound & other) const { return type < other.type; }
        Sound(Type t) : type(t) {}
    };
    
    constexpr int SAMPLE_RATE = 44100;
    
    inline int freq_to_int_period( float freq_hz ) {
        if(freq_hz <= 0.f) {
            return 1;
        }
        return (int) (((float)SAMPLE_RATE) / freq_hz);
    }
    
    struct soundId {
        soundId( Sound sound, float freq_hz )
        :
        sound(sound),
        period_length( freq_to_int_period( freq_hz ) )
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
        bool empty() const { return values.empty(); }
        
        std::vector<float> values;
        
        soundBuffer(size_t n, float value) : values(n, value) {}
        
        soundBuffer( soundId const & );
    private:
        template < typename F >
        void generate( int period, F );
    };
    
    class Sounds {
        std::map< soundId, soundBuffer > sounds;
    public:
        soundBuffer const & get( soundId const & );
    };
    
}
