
namespace imajuscule {

    constexpr int ms_to_frames(float duration_ms) {
        return static_cast<int>( SAMPLE_RATE * 0.001f * duration_ms );
    }
    
    template<typename T>
    void onQueued(T * buffer) {
        using AE = AudioElement<T>;
        A(state(buffer) == AE::inactive());
        state(buffer) = AE::queued();
    }
    
    template<typename T>
    void onUnqueued(T * buffer) {
        using AE = AudioElement<T>;
        state(buffer) = AE::inactive();
    }
    
    
    using AE32Buffer = float *;
    using AE64Buffer = double *;

    struct QueuedRequest;
    
    struct TaggedBuffer {
        friend class QueuedRequest;
        
        explicit TaggedBuffer(std::nullptr_t)
        : is_32(false), is_AudioElement(false), buffer(nullptr) {}

        explicit TaggedBuffer(soundBuffer * buf)
        : is_32(true), is_AudioElement(false), buffer(buf) {}

        explicit TaggedBuffer(AE32Buffer buf)
        : is_32(true), is_AudioElement(true), buffer(buf) {}
        
        explicit TaggedBuffer(AE64Buffer buf)
        : is_32(false), is_AudioElement(true), buffer(buf) {}
        
        void reset() {
            *this = TaggedBuffer{nullptr};
        }
        
        void reset(soundBuffer * buf) {
            *this = TaggedBuffer{buf};
        }
        
        soundBuffer::buffer & asSoundBuffer() const {
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
            buffer(soundBuffer * buf) : sound(buf ? &buf->getBuffer() : nullptr) {}
            buffer(AE32Buffer buf) : audioelement_32(buf) {}
            buffer(AE64Buffer buf) : audioelement_64(buf) {}
            
            soundBuffer::buffer * sound;
            AE32Buffer audioelement_32;
            AE64Buffer audioelement_64;
        } buffer;
        
        
        void onQueued() const {
            A(valid());
            if(isSoundBuffer()) {
                return;
            }
            if(is32()) {
                ::onQueued(asAudioElement32());
            }
            else {
                ::onQueued(asAudioElement64());
            }
        }
        
        void onUnqueued() {
            A(valid());
            if(isSoundBuffer()) {
                return;
            }
            if(is32()) {
                ::onUnqueued(asAudioElement32());
            }
            else {
                ::onUnqueued(asAudioElement64());
            }
        }
    };
    
    struct Request {
        
        Request( Sounds & sounds, Sound const sound, float freq_hz, float volume, float duration_ms );
        
        Request( soundBuffer * buffer, float volume, int duration_in_frames) :
        buffer(buffer), volume(volume), duration_in_frames(duration_in_frames) {}

        Request( AE32Buffer buffer, float volume, int duration_in_frames) :
        buffer(buffer), volume(volume), duration_in_frames(duration_in_frames) {}
        
        Request( AE64Buffer buffer, float volume, int duration_in_frames) :
        buffer(buffer), volume(volume), duration_in_frames(duration_in_frames) {}

        Request( AE32Buffer buffer, float volume, float duration_in_ms) :
        Request(buffer, volume, ms_to_frames(duration_in_ms)) {}
        
        Request( AE64Buffer buffer, float volume, float duration_in_ms) :
        Request(buffer, volume, ms_to_frames(duration_in_ms)) {}
        
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
    
    struct QueuedRequest : public Request {
        QueuedRequest() = default;
        
        // non copyable (cf. destructor implementation)
        QueuedRequest(const QueuedRequest &) = delete;
        QueuedRequest & operator=(const QueuedRequest&) = delete;
        
        // movable
        QueuedRequest(QueuedRequest && o) : Request(std::move(o)) {
            o.buffer.reset();
            A(!o.buffer);
        }
        
        QueuedRequest& operator =(QueuedRequest && o) {
            if (this != &o) {
                unqueue();
                buffer = o.buffer;
                volume = o.volume;
                duration_in_frames = o.duration_in_frames;
                o.buffer.reset();
                A(!o.buffer);
            }
            return *this;
        }

        QueuedRequest(Request const & o) : Request(o) {
            buffer.onQueued();
        }
        
        ~QueuedRequest() {
            unqueue();
        }
        
    private:
        void unqueue() {
            if(buffer) {
                buffer.onUnqueued();
            }
        }
    };
}
