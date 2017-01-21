
namespace imajuscule {
    namespace audioelement {
        template<typename T>
        void onQueued(T * buffer) {
            using AE = AudioElement<T>;
            A(state(buffer) == AE::inactive()); // to be sure at most one channel uses it
            state(buffer) = AE::queued();
        }
        
        template<typename T>
        void onUnqueued(T * buffer) {
            using AE = AudioElement<T>;
            A(state(buffer) != AE::inactive()); // to be sure at most one channel uses it
            // note that if state is AE::queued(), it means no computation occured on this buffer
            // (indicating the channel has been interrupted)
            state(buffer) = AE::inactive();
        }
    }
    
    using AE32Buffer = float *;
    using AE64Buffer = double *;

    struct QueuedRequest;

    struct TaggedBuffer {
        friend class QueuedRequest;
        
        template<typename T>
        explicit TaggedBuffer(T buf)
        : buffer(buf) {}
        
        void reset() {
            *this = TaggedBuffer{nullptr};
        }
        
        void reset(soundBuffer * buf) {
            *this = TaggedBuffer{buf};
        }
        
        soundBuffer::buffer & asSoundBuffer() const {
            auto ptr = buffer.soundBuffer();
            A(is32() && !isAudioElement() && ptr);
            return *ptr;
        }
        
        AE32Buffer asAudioElement32() const {
            auto ptr = buffer.audioElement32();
            A(is32() && isAudioElement() && ptr);
            return ptr;
        }
        
        AE64Buffer asAudioElement64() const {
            auto ptr = buffer.audioElement64();
            A(!is32() && isAudioElement() && ptr);
            return ptr;
        }
        
        bool null() const {
            // this is not strictly legal, as we dont check the tags
            // to see which of the union members is active...
            return !buffer.soundBuffer();
        }
        
        bool valid() const {
            if(isAudioElement()) {
                return !null();
            }
            if(!is32()) {
                return false;
            }
            auto ptr = buffer.soundBuffer();
            return ptr && !ptr->empty();
        }
        
        bool isSoundBuffer() const { return !buffer.flags.is_AudioElement; }
        bool isAudioElement() const { return buffer.flags.is_AudioElement; }

        bool is32() const { return buffer.flags.is_32; }

        bool isSilence() const {
            if(isAudioElement()) {
                // we cannot presume anything because
                // values are not yet computed
                return false;
            }
            if(!is32()) {
                A(0);
                return false;
            }
            if(!buffer.soundBuffer()) {
                return true;
            }
            for(auto b : *buffer.soundBuffer()) {
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
        union buffer {
            buffer(std::nullptr_t) : sound(nullptr) {
                A(as_uintptr_t == ptr());
                A(!flags.is_32);
                A(!flags.is_AudioElement);
            }
            buffer(soundBuffer * buf) : sound(buf ? &buf->getBuffer() : nullptr) {
                A(as_uintptr_t == ptr());
                flags.is_32 = true;
                A(!flags.is_AudioElement);
            }
            buffer(AE32Buffer buf) : audioelement_32(buf) {
                A(buf == audioElement32());
                flags.is_32 = true;
                flags.is_AudioElement = true;
            }
            buffer(AE64Buffer buf) : audioelement_64(buf) {
                A(buf == audioElement64());
                A(!flags.is_32);
                flags.is_AudioElement = true;
            }

            static constexpr auto n_low_bits_used = 2;
            
            uintptr_t ptr() const { return removeLowBits<n_low_bits_used>(as_uintptr_t); }
            
            soundBuffer::buffer * soundBuffer() const { return reinterpret_cast<soundBuffer::buffer *>(ptr()); }
            AE32Buffer audioElement32() const { return reinterpret_cast<AE32Buffer>(ptr()); }
            AE64Buffer audioElement64() const { return reinterpret_cast<AE64Buffer>(ptr()); }
            
            // all buffers are aligned on cachelines (64=2^6) meaning the 6 lower bits my be used to store information
            struct {
                bool is_AudioElement : 1;
                bool is_32 : 1;
            } flags;
            
            uintptr_t as_uintptr_t;
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
                audioelement::onQueued(asAudioElement32());
            }
            else {
                audioelement::onQueued(asAudioElement64());
            }
        }
        
        void onUnqueued() {
            A(valid());
            if(isSoundBuffer()) {
                return;
            }
            if(is32()) {
                audioelement::onUnqueued(asAudioElement32());
            }
            else {
                audioelement::onUnqueued(asAudioElement64());
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
