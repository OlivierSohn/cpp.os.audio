
namespace imajuscule {

    struct MakeVolume {
        template<int J = nAudioOut>
        static
        typename std::enable_if<J == 1, typename std::array<float, J>>::type
        run(float volume, StereoGain const & ) {
            return {{volume}};
        }
        
        template<int J = nAudioOut>
        static
        typename std::enable_if<J == 2, typename std::array<float, J>>::type
        run(float volume, StereoGain const & gain) {
            return {{gain.left*volume, gain.right*volume}};
        }
    };
    
    enum ChannelClosingPolicy {
        AutoClose,  // once the request queue is empty (or more precisely
                    // once the channel method isPlaying() returns false),
                    // the channel can be automatically reassigned without
                    // the need to close it explicitely.
                    // Explicitely closing an AutoClose channel will result in undefined behaviour.
        ExplicitClose, // the channel cannot be reassigned unless explicitely closed
    };
    
    /*
     * Ensures that xfade length is odd,
     * and at least 3
     */
    template<class T>
    constexpr int clamp_xfade(T xfade_) {
        int xfade = xfade_;
        if(xfade < 3) {
            return 3;
        }
        if(0 == xfade % 2) {
            ++xfade;
        }
        return xfade;
    }
    
    struct Channel : public NonCopyable {
        
        static constexpr float base_amplitude = 0.1f; // ok to have 10 chanels at max amplitude at the same time
        static constexpr unsigned int volume_transition_length = 2000;
        // make sure we'll have no overflow on volume_transition_remaining
        static_assert(volume_transition_length < (1 << 16), "");

        uint16_t volume_transition_remaining;

    private:
        // if next is false, the current crossfade is between two requests,
        // else the current crossfade is from or to 'empty'
        bool next : 1;
        
        unsigned int total_n_writes : relevantBits(audioelement::n_frames_per_buffer);
        unsigned int initial_audio_element_consummed : relevantBits(audioelement::n_frames_per_buffer - 1);
        
        QueuedRequest current;
        QueuedRequest previous;
        int size_half_xfade;
        int32_t remaining_samples_count = 0;
        int32_t current_next_sample_index = 0;
        int32_t other_next_sample_index = 0;
        
    public:
        struct Volume {
            float current = 1.f;
            float increments = 0.f;
        };
        std::array<Volume, nAudioOut> volumes;
        
    private:
        std::queue<QueuedRequest> requests;

    public:
        
        void set_xfade(int const size_xfade) {
            A(!isPlaying()); // do not call this method while playing
            A( 1 == size_xfade % 2);
            
            size_half_xfade = (size_xfade-1) / 2;
            A(size_half_xfade > 0);
        }
        
        int get_size_xfade() const { return 1 + 2 * size_half_xfade; }
        float get_xfade_increment() const { return 1.f / (get_size_xfade() - 1); };
        float duration_millis_xfade() const { return frames_to_ms(static_cast<float>(get_size_xfade())); }
        
        Channel() : volume_transition_remaining(0), next(false)
        {
            A(!isPlaying());
        }
        
        void step(SAMPLE * outputBuffer, int nFrames, unsigned int audio_element_consummed);
        
        bool addRequest(Request r) {
            if(r.duration_in_frames < 2*get_size_xfade()) {
                return false;
            }
            requests.emplace(std::move(r));
            return true;
        }

        void stopPlaying() {
            *this = Channel{};
            A(!isPlaying());
        }
        
        bool isPlaying() const {
            return remaining_samples_count != 0 || !requests.empty() || current.buffer;
        }
    private:
        
        bool consume() {
            auto current_next_sample_index_backup = current_next_sample_index;
            A(remaining_samples_count == 0);
            if (requests.empty()) {
                A(!next); // because we have started the crossfade and have detected that there is no more requests to process
                if(!current.buffer) {
                    previous = std::move(current);
                    return false;
                }
                previous = std::move(current);
                A(!current.buffer); // move reset it
                // emulate a right xfade 'to zero'
                current.duration_in_frames = get_size_xfade()-1; // to do the right xfade
                remaining_samples_count = size_half_xfade;  // to do the right xfade
                current_next_sample_index = 0;
            }
            else if(!next && !current.buffer) {
                previous = std::move(current);
                // emulate a left xfade 'from zero'
                current.duration_in_frames = 2 * get_size_xfade(); // to skip the right xfade
                remaining_samples_count = size_half_xfade + 1; // to skip the normal writes and begin the left xfade
            }
            else {
                previous = std::move(current);
                current = std::move(requests.front());
                requests.pop();
                
                A(current.duration_in_frames >= 0);
                remaining_samples_count = current.duration_in_frames;
                current_next_sample_index = next ? other_next_sample_index : 0;
            }
            other_next_sample_index = current_next_sample_index_backup;
            return true;
        }
        
        void write_single(SAMPLE * outputBuffer, int n_writes) {
            A(n_writes > 0);
            //LG(INFO, "write %d", n_writes);
            auto const volume = base_amplitude * current.volume;
            if(current.buffer.isSoundBuffer()) {
                write_single_SoundBuffer(outputBuffer, n_writes, current.buffer.asSoundBuffer(), volume);
            }
            else if(current.buffer.is32()) {
                write_single_AudioElement(outputBuffer, n_writes, current.buffer.asAudioElement32(), volume);
            }
            else {
                write_single_AudioElement(outputBuffer, n_writes, current.buffer.asAudioElement64(), volume);
            }
        }
        
        void write_single_SoundBuffer(SAMPLE * outputBuffer, int n_writes, soundBuffer::buffer const & buf, float volume) {
            auto const s = (int) buf.size();
            for( int i=0; i<n_writes; ++i) {
                if( current_next_sample_index == s ) {
                    current_next_sample_index = 0;
                }
                A(current_next_sample_index < s);
                
                A(crossfading_from_zero_remaining() <= 0);
                A(std::abs(buf[current_next_sample_index]) < 1.1f);
                auto val = volume * buf[current_next_sample_index];
                stepVolume();
                for(auto i=0; i<nAudioOut; ++i) {
                    *outputBuffer += val * volumes[i].current;
                    ++outputBuffer;
                }
                ++current_next_sample_index;
            }
        }

        template<typename T>
        void write_single_AudioElement(SAMPLE * outputBuffer, int n_writes, T const & buf, float volume) {
            for( int i=0; i<n_writes; ++i) {
                A(current_next_sample_index < audioelement::n_frames_per_buffer);
                
                A(crossfading_from_zero_remaining() <= 0);
                A(std::abs(buf[current_next_sample_index]) < 1.1f);
                auto val = volume * static_cast<float>(buf[current_next_sample_index]);
                stepVolume();
                for(auto i=0; i<nAudioOut; ++i) {
                    *outputBuffer += val * volumes[i].current;
                    ++outputBuffer;
                }
                ++current_next_sample_index;
            }

            if(current_next_sample_index == audioelement::n_frames_per_buffer) {
                current_next_sample_index = 0;
            }
        }
        
        void write_left_xfade(SAMPLE * outputBuffer, float xfade_ratio, int const n_writes) {
            A(n_writes > 0);
            //LG(INFO, "<<<<< %d", n_writes);
            A(n_writes <= remaining_samples_count);
            A(xfade_ratio >= 0.f);
            A(xfade_ratio <= 1.f);
            
            auto xfade_increment = get_xfade_increment();
            
            if(current.buffer.isSoundBuffer()) {
                if(!next || requests.front().buffer.isSoundBuffer()) {
                    auto * other = next ? &requests.front() : nullptr;
                    write_SoundBuffer_2_SoundBuffer_xfade(outputBuffer, xfade_ratio, n_writes, xfade_increment, other);
                }
                else {
                    A(next);
                    auto * other = &requests.front();
                    write_SoundBuffer_2_AudioElement_xfade(outputBuffer, xfade_ratio, n_writes, xfade_increment, other);
                }
            } else {
                auto * other = next ? &requests.front() : nullptr;
                
                if(other && other->buffer.isAudioElement()) {
                    write_AudioElement_2_AudioElement_xfade(outputBuffer, xfade_ratio, n_writes, xfade_increment, other);
                }
                else {
                    write_AudioElement_2_SoundBuffer_xfade(outputBuffer, xfade_ratio, n_writes, xfade_increment, other);
                }
            }
        }

        void write_right_xfade(SAMPLE * outputBuffer, float xfade_ratio, int const n_writes) {
            A(n_writes > 0);
            //LG(INFO, ">>>>> %d", n_writes);
            A(n_writes <= crossfading_from_zero_remaining());
            A(xfade_ratio >= 0.f);
            A(xfade_ratio <= 1.f);
            
            auto xfade_decrement = - get_xfade_increment();
            xfade_ratio = 1-xfade_ratio;
            
            // end crossfade with other only if we started with him
            auto const * other = (next || !current.buffer) ? &previous : nullptr;
            
            if(!other || other->buffer.isSoundBuffer()) {
                if(!current.buffer || current.buffer.isSoundBuffer()) {
                    write_SoundBuffer_2_SoundBuffer_xfade(outputBuffer, xfade_ratio, n_writes, xfade_decrement, other);
                }
                else {
                    write_AudioElement_2_SoundBuffer_xfade(outputBuffer, xfade_ratio, n_writes, xfade_decrement, other);
                }
            } else {
                if(current.buffer && current.buffer.isAudioElement()) {
                    write_AudioElement_2_AudioElement_xfade(outputBuffer, xfade_ratio, n_writes, xfade_decrement, other);
                }
                else {
                    write_SoundBuffer_2_AudioElement_xfade(outputBuffer, xfade_ratio, n_writes, xfade_decrement, other);
                }
            }
        }
        
        void write_SoundBuffer_2_SoundBuffer_xfade(SAMPLE * outputBuffer, float xfade_ratio, int const n_writes, float xfade_increment,
                                                   Request const * other) {
            A(!other || !other->buffer || other->buffer.isSoundBuffer());
            A(!current.buffer || current.buffer.isSoundBuffer());

            int const s = current.buffer ? (int) current.buffer.asSoundBuffer().size() : 0;
            int const other_s = (other && other->buffer) ? safe_cast<int>(other->buffer.asSoundBuffer().size()) : 0;
            for( int i=0; i<n_writes; i++ ) {
                auto val = 0.f;
                if(s) {
                    if( current_next_sample_index == s ) {
                        current_next_sample_index = 0;
                    }
                    A(current_next_sample_index < s);
                    A(std::abs(current.buffer.asSoundBuffer()[current_next_sample_index]) < 1.1f);
                    val = xfade_ratio * current.volume * current.buffer.asSoundBuffer()[current_next_sample_index];
                    ++current_next_sample_index;
                }
                if(other_s) {
                    A(other_next_sample_index >= 0);
                    A(other_next_sample_index <= other_s);
                    if(other_next_sample_index == other_s) {
                        other_next_sample_index = 0;
                    }
                    A(other_next_sample_index <= other_s);
                    A(std::abs((other->buffer.asSoundBuffer())[other_next_sample_index] < 1.1f));
                    val += (1.f - xfade_ratio) * other->volume * (other->buffer.asSoundBuffer())[other_next_sample_index];
                    ++other_next_sample_index;
                }
                xfade_ratio -= xfade_increment;
                write_value(val, outputBuffer);
            }
        }
        
        void write_SoundBuffer_2_AudioElement_xfade(SAMPLE * outputBuffer, float xfade_ratio, int const n_writes, float xfade_increment,
                                                    Request const * other) {
            A(!current.buffer || current.buffer.isSoundBuffer());
            A(other->buffer.isAudioElement());
            if(other->buffer.is32()) {
                write_SoundBuffer_2_AudioElement_xfade(outputBuffer, xfade_ratio, n_writes, xfade_increment,
                                                       other->buffer.asAudioElement32(),
                                                       other->volume);
            }
            else {
                write_SoundBuffer_2_AudioElement_xfade(outputBuffer, xfade_ratio, n_writes, xfade_increment,
                                                       other->buffer.asAudioElement64(),
                                                       other->volume);
            }
        }

        template<typename T>
        void write_SoundBuffer_2_AudioElement_xfade(SAMPLE * outputBuffer, float xfade_ratio, int const n_writes, float xfade_increment,
                                                    T const & buf2, float const volBuf2) {
            int const s = current.buffer ? (int) current.buffer.asSoundBuffer().size() : 0;
            for( int i=0; i<n_writes; i++ ) {
                A(other_next_sample_index >= 0);
                A(other_next_sample_index < audioelement::n_frames_per_buffer);
                A(std::abs(buf2[other_next_sample_index]) < 1.1f);
                auto val = (1.f - xfade_ratio) * volBuf2 * static_cast<float>(buf2[other_next_sample_index]);
                ++other_next_sample_index;
                
                if(s) {
                    if( current_next_sample_index == s ) {
                        current_next_sample_index = 0;
                    }
                    A(current_next_sample_index < s);
                    A(std::abs(current.buffer.asSoundBuffer()[current_next_sample_index]) < 1.1f);
                    val += xfade_ratio * current.volume * current.buffer.asSoundBuffer()[current_next_sample_index];
                    ++current_next_sample_index;
                }

                xfade_ratio -= xfade_increment;
                write_value(val, outputBuffer);
            }
            
            if(other_next_sample_index == audioelement::n_frames_per_buffer) {
                other_next_sample_index = 0;
            }
        }

        void write_AudioElement_2_SoundBuffer_xfade(SAMPLE * outputBuffer, float xfade_ratio, int const n_writes, float xfade_increment,
                                                    Request const * other) {
            A(current.buffer.isAudioElement());
            A(!other || !other->buffer || other->buffer.isSoundBuffer());
            if(current.buffer.is32()) {
                write_AudioElement_2_SoundBuffer_xfade(outputBuffer, xfade_ratio, n_writes, xfade_increment,
                                                       current.buffer.asAudioElement32(),
                                                       current.volume,
                                                       other);
            }
            else {
                write_AudioElement_2_SoundBuffer_xfade(outputBuffer, xfade_ratio, n_writes, xfade_increment,
                                                       current.buffer.asAudioElement64(),
                                                       current.volume,
                                                       other);
            }
        }
        
        template<typename T>
        void write_AudioElement_2_SoundBuffer_xfade(SAMPLE * outputBuffer, float xfade_ratio, int const n_writes, float xfade_increment,
                                                        T const & buf1, float const volBuf1
                                                        , Request const * other) {
            int const other_s = (other && other->buffer) ? safe_cast<int>(other->buffer.asSoundBuffer().size()) : 0;
            for( int i=0; i<n_writes; ++i, ++current_next_sample_index) {
                A(current_next_sample_index >= 0);
                A(current_next_sample_index < audioelement::n_frames_per_buffer);
                A(std::abs(buf1[current_next_sample_index]) < 1.1f);
                auto val = xfade_ratio * volBuf1 * buf1[current_next_sample_index];
                
                if(other_s) {
                    A(other_next_sample_index >= 0);
                    A(other_next_sample_index <= other_s);
                    if(other_next_sample_index == other_s) {
                        other_next_sample_index = 0;
                    }
                    A(other_next_sample_index <= other_s);
                    A(std::abs((other->buffer.asSoundBuffer())[other_next_sample_index]) < 1.1f);
                    val += (1.f - xfade_ratio) * other->volume * (other->buffer.asSoundBuffer())[other_next_sample_index];
                    ++other_next_sample_index;
                }
                xfade_ratio -= xfade_increment;
                write_value(val, outputBuffer);
            }
            
            if(current_next_sample_index == audioelement::n_frames_per_buffer) {
                current_next_sample_index = 0;
            }
        }

        void write_AudioElement_2_AudioElement_xfade(SAMPLE * outputBuffer, float xfade_ratio, int const n_writes, float xfade_increment,
                                                     Request const * other)
        {
            A(other->buffer.isAudioElement());
            A(current.buffer.isAudioElement());
            if(current.buffer.is32()) {
                if(other->buffer.is32()) {
                    write_AudioElement_2_AudioElement_xfade(outputBuffer, xfade_ratio, n_writes, xfade_increment,
                                                            current.buffer.asAudioElement32(),
                                                            current.volume,
                                                            other->buffer.asAudioElement32(),
                                                            other->volume);
                }
                else {
                    write_AudioElement_2_AudioElement_xfade(outputBuffer, xfade_ratio, n_writes, xfade_increment,
                                                            current.buffer.asAudioElement32(),
                                                            current.volume,
                                                            other->buffer.asAudioElement64(),
                                                            other->volume);
                }
            }
            else {
                if(other->buffer.is32()) {
                    write_AudioElement_2_AudioElement_xfade(outputBuffer, xfade_ratio, n_writes, xfade_increment,
                                                            current.buffer.asAudioElement64(),
                                                            current.volume,
                                                            other->buffer.asAudioElement32(),
                                                            other->volume);
                }
                else {
                    write_AudioElement_2_AudioElement_xfade(outputBuffer, xfade_ratio, n_writes, xfade_increment,
                                                            current.buffer.asAudioElement64(),
                                                            current.volume,
                                                            other->buffer.asAudioElement64(),
                                                            other->volume);
                }
            }
        }
        
        template<typename T1, typename T2>
        void write_AudioElement_2_AudioElement_xfade(SAMPLE * outputBuffer, float xfade_ratio, int const n_writes, float xfade_increment,
                                                         T1 const & buf1, float const volBuf1,
                                                         T2 const & buf2, float const volBuf2) {
            for( int i=0; i<n_writes; ++i, ++current_next_sample_index, ++other_next_sample_index, xfade_ratio -= xfade_increment) {
                A(current_next_sample_index >= 0);
                A(current_next_sample_index < audioelement::n_frames_per_buffer);
                A(other_next_sample_index >= 0);
                A(other_next_sample_index < audioelement::n_frames_per_buffer);
                
                A(std::abs(buf1[current_next_sample_index]) < 1.1f);
                A(std::abs(buf2[other_next_sample_index]) < 1.1f);

                write_value(
                            (xfade_ratio * volBuf1 * static_cast<float>(buf1[current_next_sample_index])) +
                            (1.f - xfade_ratio) * volBuf2 * static_cast<float>(buf2[other_next_sample_index])
                            ,
                            outputBuffer);
            }
            
            if(current_next_sample_index == audioelement::n_frames_per_buffer) {
                current_next_sample_index = 0;
            }
            if(other_next_sample_index == audioelement::n_frames_per_buffer) {
                other_next_sample_index = 0;
            }
        }
        
        void stepVolume() {
            if( 0 == volume_transition_remaining ) {
                return;
            }
            volume_transition_remaining--;
            for(auto i=0; i<nAudioOut; ++i) {
                volumes[i].current += volumes[i].increments;
            }
        }

        void write_value(SAMPLE val, SAMPLE *& outputBuffer) {
            stepVolume();
            val *= base_amplitude;
            for(auto i=0; i<nAudioOut; ++i) {
                *outputBuffer += val * volumes[i].current;
                ++outputBuffer;
            }
        }
        
        int crossfading_from_zero_remaining() const {
            if(next) {
                return size_half_xfade - (current.duration_in_frames - remaining_samples_count);
            }
            else {
                return (get_size_xfade()-1) - (current.duration_in_frames - remaining_samples_count);
            }
        }
        
        void onBeginToZero(int n_writes_remaining) {
            if((next = !requests.empty())) {
                
                if(requests.front().buffer.isSoundBuffer()) {
                    // soundBuffer are "synchronized" when possible : a sinus will start at the first positive value of sin, and end at 0
                    // so we want to start playing the next soundBuffer so that at the middle of the crossfade, it is exactly
                    // at the first sample of the buffer.
                
                    int sz_buffer = safe_cast<int>(requests.front().buffer.asSoundBuffer().size());
                    other_next_sample_index = sz_buffer - 1 - size_half_xfade;
                    while(other_next_sample_index < 0) {
                        other_next_sample_index += sz_buffer;
                    }
                }
                else {
                    A(n_writes_remaining > 0);
                    
                    other_next_sample_index = initial_audio_element_consummed; // keep separate to make the type consversion
                    A(n_writes_remaining <= total_n_writes); // make sure it's safe to do the following substraction, total_n_writes being unsigned
                    other_next_sample_index += total_n_writes - n_writes_remaining;
                    A(other_next_sample_index < audioelement::n_frames_per_buffer);
                }
                A(other_next_sample_index >= 0);
            }
        }
        
        bool handleToZero(SAMPLE *& outputBuffer, int & n_max_writes) {
            if(remaining_samples_count == size_half_xfade + 1) {
                onBeginToZero(n_max_writes);
            }
            auto xfade_ratio = 0.5f + (float)(remaining_samples_count-1) / (float)(2*size_half_xfade);
            auto xfade_written = std::min(remaining_samples_count, n_max_writes);
            write_left_xfade( outputBuffer, xfade_ratio, xfade_written );
            n_max_writes -= xfade_written;
            remaining_samples_count -= xfade_written;
            if(n_max_writes <= 0) {
                return false;
            }
            outputBuffer += xfade_written * nAudioOut;
            A(remaining_samples_count == 0); // we are sure the xfade is finished
            return consume();
        }
    };
}
