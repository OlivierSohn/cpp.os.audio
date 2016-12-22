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

typedef void PaStream; //#include "portaudio.h"


namespace imajuscule {
    
    struct Sound {
        bool zeroOnPeriodBoundaries() const { return type == SINE || type == TRIANGLE; }
        enum Type : unsigned char {SINE, TRIANGLE, SAW, SQUARE, NOISE, SILENCE } type : 3;
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
        soundId( Sound sound, float freq_hz ) : sound(sound), period_length( freq_to_int_period( freq_hz ) ) {
            A( period_length >= 1 );
        }
        Sound sound;
        int period_length;
        bool operator < (const soundId & other) const {
            if(sound == other.sound) {
                return ( period_length < other.period_length );
            }
            return( sound < other.sound );
        }
    };
    
    struct soundBuffer {
        std::vector<float> values;
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
    
    struct Request {
        Request( Sounds & sounds, Sound const & sound, float freq_hz, float volume, float duration_ms );
        
        soundBuffer const * buffer;
        float volume;
        int duration_in_samples;
    };
    
    // reserved number to indicate "no channel"
    enum : uint8_t {AUDIO_CHANNEL_NONE = std::numeric_limits<uint8_t>::max()};
    
    struct outputData {
    private:
        
        struct Channel {
            void step(SAMPLE * outputBuffer, int framesPerBuffer);
            
            // to play a sound (loop over a waveform) for a given duration
            struct Playing {
                int remaining_samples_count = 0;
                int next_sample_index = 0;
                soundBuffer const * sound = nullptr;
                float sound_volume = 0.f;
                
                enum { volume_transition_length = 2000 };
                int transition_volume_remaining = 0;
                float channel_volume = 1.f;
                float channel_volume_increments = 0.f;
                
                void consume( std::queue<Request> & requests) {
                    play(requests.front());
                    requests.pop();
                }
                
                void write(SAMPLE * outputBuffer, int framesPerBuffer);
            private:
                void play(Request & r) {
                    sound = r.buffer;
                    sound_volume = r.volume;
                    A(r.duration_in_samples >= 0);
                    remaining_samples_count = r.duration_in_samples;
                    next_sample_index = 0;
                }
            } playing;
            
            std::queue<Request> requests;
        };
        
        static uint8_t take_available_id();
        AvailableIndexes<uint8_t> available_ids;
        std::vector<Channel> channels;
        std::atomic_bool used { false }; // maybe we need two level of locks, one here for the vector and many inside for the elements
        
    public:
        outputData();
        
        // called from audio callback
        void step(SAMPLE * outputBuffer, int framesPerBuffer);
        
        struct DelayLine {
            DelayLine(int size, float attenuation);
            void step(SAMPLE * outputBuffer, int framesPerBuffer);
            std::vector<float> delay;
            int it, end;
            float attenuation;
        };
        std::vector< DelayLine > delays;
        
#if TARGET_OS_IOS
        std::vector<float> outputBuffer;
#endif
        
        // called from main thread
        uint8_t openChannel();
        Channel & editChannel(uint8_t id) { return channels[id]; }
        void play( uint8_t channel_id, std::vector<Request> && );
        void setVolume( uint8_t channel_id, float vol );
        bool closeChannel(uint8_t channel_id);
    };    
}
