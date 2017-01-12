
namespace imajuscule {

    struct Request {
        Request( Sounds & sounds, Sound const sound, float freq_hz, float volume, float duration_ms );
        
        Request( soundBuffer const *buffer, float volume, int duration_in_frames) :
        buffer(buffer), volume(volume), duration_in_frames(duration_in_frames) {}
        
        Request() : buffer(nullptr) {}
        
        void reset() { buffer = nullptr; }
        
        bool valid() const { return duration_in_frames >= 1 && buffer && !buffer->empty(); }

        bool isSilence() const {
            if( volume == 0.f || !buffer ) {
                return true;
            }
            for(auto b : *buffer) {
                if(b != 0.f) {
                    return false;
                }
            }
            return true;
        }
        
        soundBuffer const * buffer;
        float volume;
        int32_t duration_in_frames;
    };
}
