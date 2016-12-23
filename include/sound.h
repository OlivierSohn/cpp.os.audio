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

// fwd decl. test classes
class AudioTest_Xfade_consecutive_sounds_Test;
class AudioTest_Xfade_from_empty_Test;
class AudioTest_Xfade_to_empty_Test;
class AudioTest_Compare_silence_empty_Test;

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
    
    struct Request {
        Request( Sounds & sounds, Sound const & sound, float freq_hz, float volume, float duration_ms );
        
        Request( soundBuffer const *buffer, float volume, int duration_in_samples) :
        buffer(buffer), volume(volume), duration_in_samples(duration_in_samples) {}
        
        Request() : buffer(nullptr) {}

        void reset() { buffer = nullptr; }
        
        soundBuffer const * buffer;
        float volume;
        int duration_in_samples;
    };
    
    // reserved number to indicate "no channel"
    enum : uint8_t {AUDIO_CHANNEL_NONE = std::numeric_limits<uint8_t>::max()};
    
    struct outputData {
        friend class ::AudioTest_Xfade_consecutive_sounds_Test;
        friend class ::AudioTest_Xfade_from_empty_Test;
        friend class ::AudioTest_Xfade_to_empty_Test;
        friend class ::AudioTest_Compare_silence_empty_Test;

        static constexpr float amplitude = 0.1f; // ok to have 10 chanels at max amplitude at the same time

    private:
        
        struct Channel {
            
            Channel() : transition_volume_remaining(0) {}
            
            void step(SAMPLE * outputBuffer, int framesPerBuffer);
            
            static constexpr int size_xfade = 201;
            static constexpr unsigned int volume_transition_length = 2000;
            // make sure we'll have no overflow on transition_volume_remaining
            static_assert(volume_transition_length < (1 << 16), "");
            unsigned int transition_volume_remaining : 16;
        private:
            // todo us a bit for that
            Request * next = nullptr; // if non null, the current crossfade is between two requests,
                                      // else the current crossfade is from or to zero
            Request current;
            Request previous;
            int remaining_samples_count = 0;
            int next_sample_index = 0;
            int previous_next_sample_index = 0;

        public:
            float channel_volume = 1.f;
            float channel_volume_increments = 0.f;
        private:
            
            bool crossfade_from_zero = true;
            
            bool consume() {
                previous = current;
                A(remaining_samples_count == 0);
                if (requests.empty()) {
                    A(nullptr == next); // because we have started the crossfade and have detected that there is no more requests to process
                    if(!current.buffer) {
                        return false;
                    }
                    current.reset();
                    current.duration_in_samples = size_xfade-1; // needed for crossfading_from_zero_remaining()
                    remaining_samples_count = size_half_xfade;
                    next_sample_index = 0;
                    previous_next_sample_index = 0;
                    return true;
                }
                current = requests.front();
                requests.pop();
                
                A(current.duration_in_samples >= 0);
                remaining_samples_count = current.duration_in_samples;
                next_sample_index = 0;
                previous_next_sample_index = 0;
                return true;
            }
            
            void write(SAMPLE * outputBuffer, int framesPerBuffer);
            void write_crossfading_from_zero(SAMPLE * outputBuffer, float xfade_ratio, int framesPerBuffer);
            void write_crossfading_to_zero(SAMPLE * outputBuffer, float xfade_ratio, int framesPerBuffer);
            
            static_assert( 1 == size_xfade % 2, "");
            static constexpr int size_half_xfade = (size_xfade-1) / 2;
            
            static constexpr float xfade_increment = 1.f / static_cast<float>(size_xfade-1);
            static constexpr float inv_half_size_xfade = 1.f / static_cast<float>(size_half_xfade);
         
            int crossfading_from_zero_remaining() const {
                if(next) {
                    return size_half_xfade - (current.duration_in_samples - remaining_samples_count);
                }
                else {
                    return (size_xfade-1) - (current.duration_in_samples - remaining_samples_count);
                }
            }
        public:
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
