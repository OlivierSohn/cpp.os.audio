
namespace imajuscule {
   
    namespace Sensor {
        
        class RAIILock {
        public:
            RAIILock( std::atomic_bool & l ) : l(l) {
                bool bFalse( false );
                // TODO check the way we use locks, this is different from RAIILock in sensor.h
                
                while (!l.compare_exchange_strong(bFalse, true,
                                                  std::memory_order_acquire,
                                                  std::memory_order_relaxed))
                {}
            }
            ~RAIILock() {
                l.store(false, std::memory_order_release);
            }
        private:
            std::atomic_bool & l;
            
            RAIILock(const RAIILock &) = delete;
            RAIILock & operator = (const RAIILock &) = delete;
        };
    }
    struct DelayLine {
        DelayLine(int size, float attenuation);
        void step(SAMPLE * outputBuffer, int nFrames);
        std::vector<std::array<float, nAudioOut>> delay;
        int32_t it, end;
        float attenuation;
    };
    
    using channelVolumes = std::array<float, nAudioOut>;
    
    // reserved number to indicate "no channel"
    static constexpr auto AUDIO_CHANNEL_NONE = std::numeric_limits<uint8_t>::max();
    
    struct outputData {
    private:
        std::atomic_bool used { false }; // maybe we need two level of locks, one here for the vector and many inside for the elements
        
        //////////////////////////
        /// state of last write:
        
        bool clock_ : 1; /// "tic tac" flag, inverted at each new AudioElements buffer writes
        
        /// the number of buffer frames that were used from the previous AudioElements buffer write
        /// "0" means the entire buffers where used
        unsigned int consummed_frames : relevantBits( AudioElementBase::n_frames_per_buffer - 1 );
        ///
        //////////////////////////
        
        AvailableIndexes<uint8_t> available_ids;
        std::vector<Channel> channels;
        std::vector<uint8_t> autoclosing_ids;
        
        std::vector<std::function<void(bool)>> audioElements_computes;
        
    public:
        outputData();
        
        // called from audio callback
        void step(SAMPLE * outputBuffer, int nFrames);
        
        std::vector< DelayLine > delays;
        
#if TARGET_OS_IOS
        std::vector<float> outputBuffer;
#endif
        
        // called from main thread
        uint8_t openChannel(channelVolumes volume, ChannelClosingPolicy, int xfade_length);
        Channel & editChannel(uint8_t id) { return channels[id]; }
        Channel const & getChannel(uint8_t id) const { return channels[id]; }
        void play( uint8_t channel_id, StackVector<Request> && );
        void setVolume( uint8_t channel_id, channelVolumes );
        bool closeChannel(uint8_t channel_id);
        
        template<typename F>
        void registerAudioElementCompute(F f) {
            Sensor::RAIILock l(used);
            // todo prevent reallocation here to not block audio...
            audioElements_computes.push_back(std::move(f));
        }
        
    private:
        void computeNextAudioElementsBuffers() {
            A(consummed_frames == 0); // else we skip some unconsummed frames
            clock_ = !clock_;
            for(auto const & f : audioElements_computes) {
                f(clock_);
            }
            A(consummed_frames == 0);
        }
        
        // returns true if everything was consummed AND there is more frames remaining
        bool consume_buffers(SAMPLE *& buf, int & nFrames) {
            A(consummed_frames < AudioElementBase::n_frames_per_buffer);
            auto remaining_frames = AudioElementBase::n_frames_per_buffer - consummed_frames;
            A(remaining_frames <= AudioElementBase::n_frames_per_buffer);
            A(remaining_frames > 0);
            if(remaining_frames > nFrames) {
                // partial consume
                do_consume_buffers(buf, nFrames);
                consummed_frames += nFrames;
                A(consummed_frames < AudioElementBase::n_frames_per_buffer);
                return false;
            }
            // total consume
            do_consume_buffers(buf, remaining_frames);
            consummed_frames = 0;
            nFrames -= remaining_frames;
            if(nFrames == 0) {
                return false;
            }
            buf += nAudioOut * remaining_frames;
            return true;
        }
        
        void do_consume_buffers(SAMPLE * outputBuffer, int nFrames) {
            A(nFrames <= AudioElementBase::n_frames_per_buffer); // by design
            A(consummed_frames < AudioElementBase::n_frames_per_buffer); // by design
            
            memset(outputBuffer, 0, nFrames * nAudioOut * sizeof(SAMPLE));
            
            for( auto & c: channels ) {
                c.step(outputBuffer,
                       nFrames,
                       consummed_frames ); // with that, the channel knows when
                                           // the next computation of AudioElements will occur
            }

            // apply the effect
            for( auto & delay : delays ) {
                // deactivated on purpose : reactivate once low pass filtered
                //delay.step(outputBuffer, nFrames);
            }
        }
    };
}
