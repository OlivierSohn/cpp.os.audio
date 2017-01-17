
namespace imajuscule {

    constexpr int ms_to_frames(float duration_ms) {
        return static_cast<int>( SAMPLE_RATE * 0.001f * duration_ms );
    }
    
    using AE32Buffer = float const *;
    using AE64Buffer = double const *;

    struct TaggedBuffer {
        
        explicit TaggedBuffer(std::nullptr_t)
        : is_32(false), is_AudioElement(false), buffer(nullptr) {}

        explicit TaggedBuffer(soundBuffer const *buf)
        : is_32(true), is_AudioElement(false), buffer(buf) {}

        explicit TaggedBuffer(AE32Buffer const buf)
        : is_32(true), is_AudioElement(true), buffer(buf) {}
        
        explicit TaggedBuffer(AE64Buffer const buf)
        : is_32(false), is_AudioElement(true), buffer(buf) {}
        
        void reset() {
            *this = TaggedBuffer{nullptr};
        }
        
        void reset(soundBuffer const *buf) {
            *this = TaggedBuffer{buf};
        }
        
        soundBuffer const & asSoundBuffer() const {
            A(is_32 && !is_AudioElement && buffer.sound);
            return *buffer.sound;
        }
        
        AE32Buffer asAudioElement32() const {
            A(is_32 && is_AudioElement && buffer.audioelement_32);
            return buffer.audioelement_32;
        }
        
        AE64Buffer asAudioElement64() const {
            A(!is_32 && is_AudioElement && buffer.audioelement_64);
            return buffer.audioelement_64;
        }
        
        bool null() const {
            // this is not strictly legal, as we dont check the tags
            // to see which of the union members is active...
            return !buffer.sound;
        }
        
        bool valid() const {
            if(is_AudioElement) {
                return !null();
            }
            if(!is_32) {
                return false;
            }
            return buffer.sound && !buffer.sound->empty();
        }
        
        bool isSoundBuffer() const { return !is_AudioElement; }
        bool isAudioElement() const { return is_AudioElement; }

        bool is32() const { return is_32; }

        bool isSilence() const {
            if(is_AudioElement) {
                // we cannot presume anything because
                // values are not yet computed
                return false;
            }
            if(!is_32) {
                A(0);
                return false;
            }
            if(!buffer.sound) {
                return true;
            }
            for(auto b : *buffer.sound) {
                if(b != 0.f) {
                    return false;
                }
            }
            return true;
        }
        
        operator bool() const {
            return !null();
        }
        
    private:
        bool is_32:1;
        bool is_AudioElement:1;
        
        // all buffers are aligned on cachelines (64=2^6) meaning the 6 lower bits my be used to store information
        union buffer {
            buffer(std::nullptr_t) : sound(nullptr) {}
            buffer(soundBuffer const *buf) : sound(buf) {}
            buffer(AE32Buffer buf) : audioelement_32(buf) {}
            buffer(AE64Buffer buf) : audioelement_64(buf) {}
            
            soundBuffer const * sound;
            AE32Buffer audioelement_32;
            AE64Buffer audioelement_64;
        } buffer;
    };
    
    struct Request {
        Request( Sounds & sounds, Sound const sound, float freq_hz, float volume, float duration_ms );
        
        Request( soundBuffer const *buffer, float volume, int duration_in_frames) :
        buffer(buffer), volume(volume), duration_in_frames(duration_in_frames) {}

        Request( AE32Buffer buffer, float volume, float duration_in_ms) :
        buffer(buffer), volume(volume), duration_in_frames(ms_to_frames(duration_in_ms)) {}

        Request( AE64Buffer buffer, float volume, float duration_in_ms) :
        buffer(buffer), volume(volume), duration_in_frames(ms_to_frames(duration_in_ms)) {}
        
        Request() : buffer(nullptr) {}
        
        void reset() { buffer.reset(); }
        
        bool valid() const { return duration_in_frames >= 1 && buffer.valid(); }

        bool isSilence() const {
            return volume == 0.f || buffer.isSilence();
        }
        
        TaggedBuffer buffer;
        float volume;
        int32_t duration_in_frames;
    };
}
