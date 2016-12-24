
namespace imajuscule {

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
}
