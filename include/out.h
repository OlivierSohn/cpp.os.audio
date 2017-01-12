
// fwd decl. test classes
class AudioTest_Xfade_consecutive_sounds_Test;
class AudioTest_Xfade_from_empty_Test;
class AudioTest_Xfade_to_empty_Test;
class AudioTest_Compare_silence_empty_Test;
class AudioTest_Validate_request_size_Test;

namespace imajuscule {
    constexpr unsigned int nAudioOut = 2; // note that on some systems, if there is only one output portaudio may refuse to open the stream

    struct DelayLine {
        DelayLine(int size, float attenuation);
        void step(SAMPLE * outputBuffer, int nFrames);
        std::vector<std::array<float, nAudioOut>> delay;
        int32_t it, end;
        float attenuation;
    };

    // reserved number to indicate "no channel"
    static constexpr auto AUDIO_CHANNEL_NONE = std::numeric_limits<uint8_t>::max();
    
    using channelVolumes = std::array<float, nAudioOut>;
    
    struct MakeVolume {
        template<int J = nAudioOut>
        static
        typename std::enable_if<J == 1, typename std::array<float, J>>::type
        run(float volume, float left, float right) {
            return {{volume}};
        }
        
        template<int J = nAudioOut>
        static
        typename std::enable_if<J == 2, typename std::array<float, J>>::type
        run(float volume, float left, float right) {
            return {{left*volume, right*volume}};
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
    
    struct outputData {
        friend class ::AudioTest_Xfade_consecutive_sounds_Test;
        friend class ::AudioTest_Xfade_from_empty_Test;
        friend class ::AudioTest_Xfade_to_empty_Test;
        friend class ::AudioTest_Compare_silence_empty_Test;
        friend class ::AudioTest_Validate_request_size_Test;

        static constexpr float amplitude = 0.1f; // ok to have 10 chanels at max amplitude at the same time

    private:
        
        struct Channel {
            
            Channel() :
            volume_transition_remaining(0), next(false) {}
            
            void step(SAMPLE * outputBuffer, int nFrames);
            
            static constexpr int size_xfade = 201;
            static constexpr unsigned int volume_transition_length = 2000;
            // make sure we'll have no overflow on volume_transition_remaining
            static_assert(volume_transition_length < (1 << 16), "");
            uint16_t volume_transition_remaining;
        private:
            bool next : 1; // if false, the current crossfade is between two requests,
                           // else the current crossfade is from or to 'empty'
            Request current;
            Request previous;
            int32_t remaining_samples_count = 0;
            int32_t current_next_sample_index = 0;
            int32_t other_next_sample_index = 0;

        public:
            struct Volume {
                float current = 1.f;
                float increments = 0.f;
            };
            std::array<Volume, nAudioOut> volumes;
            
            bool addRequest(Request r) {
                if(r.duration_in_frames < 2*size_xfade) {
                    return false;
                }
                requests.emplace(std::move(r));
                return true;
            }
            
            void clear() {
                std::queue<Request> empty;
                requests.swap(empty);
            }
            
            bool isPlaying() const {
                return remaining_samples_count != 0 || !requests.empty() || current.buffer;
            }
        private:
            
            bool consume() {
                previous = current;
                A(remaining_samples_count == 0);
                if (requests.empty()) {
                    A(!next); // because we have started the crossfade and have detected that there is no more requests to process
                    if(!current.buffer) {
                        return false;
                    }
                    // emulate a right xfade 'to zero'
                    current.reset();
                    current.duration_in_frames = size_xfade-1; // to do the right xfade
                    remaining_samples_count = size_half_xfade;  // to do the right xfade
                    current_next_sample_index = 0;
                    other_next_sample_index = 0;
                }
                else if(!next && !current.buffer) {
                    // emulate a left xfade 'from zero'
                    current.duration_in_frames = 2 * size_xfade; // to skip the right xfade
                    remaining_samples_count = size_half_xfade + 1; // to skip the normal writes and begin the left xfade
                }
                else {
                    current = requests.front();
                    requests.pop();
                    
                    A(current.duration_in_frames >= 0);
                    remaining_samples_count = current.duration_in_frames;
                    current_next_sample_index = 0;
                    other_next_sample_index = 0;
                }
                return true;
            }
            
            void write(SAMPLE * outputBuffer, int nFrames);
            void write_xfade_right(SAMPLE * outputBuffer, float xfade_ratio, int const nFrames);
            void write_xfade_left(SAMPLE * outputBuffer, float xfade_ratio, int const nFrames);

            void write_value(SAMPLE val, SAMPLE *& outputBuffer) {
                if( volume_transition_remaining ) {
                    volume_transition_remaining--;
                    for(auto i=0; i<nAudioOut; ++i) {
                        volumes[i].current += volumes[i].increments;
                    }
                }
                val *= amplitude;
                for(auto i=0; i<nAudioOut; ++i) {
                    *outputBuffer += val * volumes[i].current;
                    ++outputBuffer;
                }
            }
            
            static_assert( 1 == size_xfade % 2, "");
            static constexpr int size_half_xfade = (size_xfade-1) / 2;
            
            static constexpr float xfade_increment = 1.f / (size_xfade-1);
            static constexpr float inv_half_size_xfade = 1.f / size_half_xfade;
         
            int crossfading_from_zero_remaining() const {
                if(next) {
                    return size_half_xfade - (current.duration_in_frames - remaining_samples_count);
                }
                else {
                    return (size_xfade-1) - (current.duration_in_frames - remaining_samples_count);
                }
            }
            
            void onBeginToZero() {
                if((next = !requests.empty())) {
                    int sz_buffer = safe_cast<int>(requests.front().buffer->size());
                    other_next_sample_index = ( sz_buffer - 1 - size_half_xfade) % sz_buffer;
                    if(other_next_sample_index < 0) {
                        other_next_sample_index += sz_buffer;
                    }
                    A(other_next_sample_index >= 0);
                }
            }
            
            bool handleToZero(SAMPLE *& outputBuffer, int & n_max_writes) {
                if(remaining_samples_count == size_half_xfade + 1) {
                    onBeginToZero();
                }
                auto xfade_ratio = 0.5f + 0.5f* (float)(remaining_samples_count-1) * inv_half_size_xfade;
                auto xfade_written = std::min(remaining_samples_count, n_max_writes);
                write_xfade_left( outputBuffer, xfade_ratio, xfade_written );
                n_max_writes -= xfade_written;
                remaining_samples_count -= xfade_written;
                if(n_max_writes <= 0) {
                    return false;
                }
                outputBuffer += xfade_written * nAudioOut;
                A(remaining_samples_count == 0); // we are sure the xfade is finished
                return consume();
            }
            
            std::queue<Request> requests;
        };
        
        std::atomic_bool used { false }; // maybe we need two level of locks, one here for the vector and many inside for the elements
        AvailableIndexes<uint8_t> available_ids;
        std::vector<Channel> channels;
        std::vector<uint8_t> autoclosing_ids;
        
    public:
        outputData();
        
        // called from audio callback
        void step(SAMPLE * outputBuffer, int nFrames);
        
        std::vector< DelayLine > delays;
        
#if TARGET_OS_IOS
        std::vector<float> outputBuffer;
#endif
        
        // called from main thread
        uint8_t openChannel(channelVolumes volume, ChannelClosingPolicy);
        Channel & editChannel(uint8_t id) { return channels[id]; }
        void play( uint8_t channel_id, StaticVector<Request> && );
        void setVolume( uint8_t channel_id, channelVolumes );
        bool closeChannel(uint8_t channel_id);
    };    
}
