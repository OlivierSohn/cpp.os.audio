#define WITH_DELAY 0

namespace imajuscule {
   
    namespace Sensor {
        
        class RAIILock {
        public:
            RAIILock( std::atomic_bool & l ) : l(l) {
                while (l.exchange(true)) { }
            }
            ~RAIILock() {
                l = false;
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
    
    
    template<typename T, typename Init, size_t... Inds>
    std::array<T, sizeof...(Inds)> makeArrayImpl(Init val, std::integer_sequence<size_t, Inds...>)
    {
        return { (val + (Inds - Inds))... };
    }
    
    template<typename T, int N, typename Init>
    std::array<T, N> makeArray(Init val)
    {
        return makeArrayImpl<T, Init>(val, std::make_index_sequence<N>{});
    }

    struct Compressor {
        // some parts inspired from https://github.com/audacity/audacity/blob/master/src/effects/Compressor.cpp
        
        static constexpr auto length_sliding_avg = 40;
        
        Compressor(Compressor&&) = default;
        Compressor& operator=(Compressor&&) = default;
        
        Compressor() : avgs(makeArray<slidingAverage<float, KEEP_INITIAL_VALUES>, nAudioOut>(length_sliding_avg)) {
        }
        std::array<slidingAverage<float, KEEP_INITIAL_VALUES>, nAudioOut> avgs;
        
        float threshold = 0.5f;
        static constexpr auto ratio = 3.f;
        float compression = 1.f-1.f/ratio;
        float compute(float value, float env)
        {
            if(env <= 0.f) {
                return 0.f;
            }
            return value * powf(threshold/env, compression);
        }
    };

    using channelVolumes = std::array<float, nAudioOut>;
    
    // reserved number to indicate "no channel"
    static constexpr auto AUDIO_CHANNEL_NONE = std::numeric_limits<uint8_t>::max();
    
    enum class CloseMode {
        FORCE, // channel is closed now even if it is playing something
        SOFT   // channel will be converted to autoclosing
    };
    
    enum class PostProcess {
        COMPRESS,
        NONE
    };
    template<PostProcess Post = PostProcess::NONE>
    struct outputDataBase {
    private:
        std::atomic_bool used { false }; // maybe we need two level of locks, one here for the vector and many inside for the elements
        
        //////////////////////////
        /// state of last write:
        
        bool clock_ : 1; /// "tic tac" flag, inverted at each new AudioElements buffer writes
        
        /// the number of buffer frames that were used from the previous AudioElements buffer write
        /// "0" means the entire buffers where used
        unsigned int consummed_frames : relevantBits( audioelement::n_frames_per_buffer - 1 );
        ///
        //////////////////////////
        
        AvailableIndexes<uint8_t> available_ids;
        std::vector<Channel> channels;
        std::vector<uint8_t> autoclosing_ids;

        // this could be replaced by traversing the pool of AudioElements, and
        // computing the ones that are active.
        using postProcessFunc = std::function<void(float*)>;

        std::vector<audioelement::ComputeFunc> audioElements_computes;
        std::vector<postProcessFunc> post_process;
        
#if WITH_DELAY
        std::vector< DelayLine > delays;
#endif
        
#if TARGET_OS_IOS
    public:
        std::vector<float> outputBuffer;
    private:
#endif
        
    private:
        template<typename F>
        void registerAudioElementCompute(F f) {
            // todo prevent reallocation here to not block audio...
            audioElements_computes.push_back(std::move(f));
        }
        
    public:
        outputDataBase()
        :
#if WITH_DELAY
        delays{{1000, 0.6f},{4000, 0.2f}, {4300, 0.3f}, {5000, 0.1f}},
#endif
        clock_(false),
        consummed_frames(0)
        {
            // to avoid reallocations when we hold the lock
            // we allocate all we need for channel management now:
            channels.reserve(std::numeric_limits<uint8_t>::max());
            autoclosing_ids.reserve(std::numeric_limits<uint8_t>::max());
            available_ids.reserve(std::numeric_limits<uint8_t>::max());
            if(Post == PostProcess::COMPRESS) {
                // doesn't work : in TEST_F(AudioLimiting, CompressionSine),
                // the beginning is wrong.
                // I should rethink the way to do it. I'm not even sure I will
                // need that in the future.

                /*
                post_process.push_back([](float v[nAudioOut]) mutable {
                    static Compressor c;
                    float avg = 0.f;
                    for(int i=0; i<nAudioOut; ++i) {
                        c.avgs[i].feed(std::abs(v[i]));
                        avg = std::max(avg, c.avgs[i].compute());
                    }
                    for(int i=0; i<nAudioOut; ++i) {
                        v[i] = c.compute(v[i], avg);
                    }
                });
                 */
            }
            // hard limit
            post_process.emplace_back([](float v[nAudioOut]) {
                for(int i=0; i<nAudioOut; ++i) {
                    if(likely(-1.f <= v[i] && v[i] <= 1.f)) {
                        continue;
                    }
                    
                    if(v[i] > 1.f) {
                        v[i] = 1.f;
                    }
                    else if(v[i] < -1.f) {
                        v[i] = -1.f;
                    }
                    else {
                        v[i] = 0.f; // v[i] is NaN
                    }
                }
            });
        }

        // called from audio callback
        void step(SAMPLE *outputBuffer, int nFrames) {
            Sensor::RAIILock l(used);
            
            if(consummed_frames != 0) {
                // finish consuming previous buffers
                if(!consume_buffers(outputBuffer, nFrames)) {
                    return;
                }
            }
            
            while(true) {
                // the previous buffers are consumed, we need to compute them again
                computeNextAudioElementsBuffers();
                
                if(!consume_buffers(outputBuffer, nFrames)) {
                    return;
                }
            }
        }
        
        // called from main thread
        Channel & editChannel(uint8_t id) { return channels[id]; }
        Channel const & getChannel(uint8_t id) const { return channels[id]; }
        bool empty() const { return channels.empty(); }
        void setVolume(uint8_t channel_id, channelVolumes volumes) {
            // no need to lock, this is called from the main thread
            auto & c = editChannel(channel_id);
            c.volume_transition_remaining = Channel::volume_transition_length;
            for(int i=0; i<nAudioOut; ++i) {
                c.volumes[i].increments = (volumes[i] - c.volumes[i].current) / (float) Channel::volume_transition_length;
            }
        }
        
        template<class... Args>
        void playGeneric( uint8_t channel_id, Args&&... requests) {
            Sensor::RAIILock l(used);
            
            // it's important to register and enqueue in the same lock cycle
            // else either it's too late and we miss some audio frames,
            // or too early and the callback gets unscheduled
            
            auto buffers = std::make_tuple(std::ref(requests.first)...);
            for_each(buffers, [this](auto &buf) {
                if(auto f = audioelement::fCompute(buf)) {
                    this->registerAudioElementCompute(std::move(f));
                }
            });
            
            playNolock(channel_id, {std::move(requests.second)...});
        }
        
        void play( uint8_t channel_id, StackVector<Request> && v) {
            Sensor::RAIILock l(used);
            playNolock(channel_id, std::move(v));
        }
        
        void closeAllChannels() {
            Sensor::RAIILock l(used);
            channels.clear();
        }

        uint8_t openChannel(channelVolumes volume, ChannelClosingPolicy l, int xfade_length) {
            uint8_t id = AUDIO_CHANNEL_NONE;
            if(channels.size() == std::numeric_limits<uint8_t>::max() && available_ids.size() == 0) {
                // Channels are at their maximum number and all are used...
                // Let's find one that is autoclosing and not playing :
                for( auto it = autoclosing_ids.begin(), end = autoclosing_ids.end(); it != end; ++it )
                {
                    id = *it;
                    {
                        // take the lock in the loop so that at the end of each iteration
                        // the audio thread has a chance to run
                        Sensor::RAIILock l(used);
                        if(channels[id].isPlaying()) {
                            continue;
                        }
                    }
                    // channel 'id' is auto closing and not playing, so we will assign it to the caller.
                    if(l != AutoClose) {
                        autoclosing_ids.erase(it);
                    }
                    break;
                }
            }
            else {
                id = available_ids.Take(channels);
                if(l == AutoClose) {
                    autoclosing_ids.push_back(id);
                    A(autoclosing_ids.size() <= std::numeric_limits<uint8_t>::max());
                    // else logic error : some users closed manually some autoclosing channels
                }
            }
            // no need to lock here : the channel is not active
            for(auto i=0; i<nAudioOut; ++i) {
                editChannel(id).volumes[i].current = volume[i];
            }
            editChannel(id).set_xfade(xfade_length);
            A(id != AUDIO_CHANNEL_NONE);
            return id;
        }
        
        void closeChannel(uint8_t channel_id, CloseMode mode)
        {
#ifndef NDEBUG
            auto it = std::find(autoclosing_ids.begin(), autoclosing_ids.end(), channel_id);
            A(it == autoclosing_ids.end()); // make sure channel is NOT autoclosing
#endif
            {
                Sensor::RAIILock l(used);
                auto & c = editChannel(channel_id);
                if(mode == CloseMode::SOFT && c.isPlaying()) {
                    autoclosing_ids.push_back(channel_id);
                    A(autoclosing_ids.size() <= std::numeric_limits<uint8_t>::max());
                    // else logic error : some users closed manually some autoclosing channels
                    return;
                }
                c.stopPlaying();
            }
            available_ids.Return(channel_id);
        }
        
    private:
        void playNolock( uint8_t channel_id, StackVector<Request> && v) {
            auto & c = editChannel(channel_id);
            for( auto & sound : v ) {
                c.addRequest( std::move(sound) );
            }
        }

        void computeNextAudioElementsBuffers() {
            A(consummed_frames == 0); // else we skip some unconsummed frames
            clock_ = !clock_;
            for(auto it = audioElements_computes.begin(),
                end = audioElements_computes.end(); it!=end;) {
                if(!((*it)(clock_))) {
                    it = audioElements_computes.erase(it);
                    end = audioElements_computes.end();
                }
                else {
                    ++it;
                }
            }
            A(consummed_frames == 0);
        }
        
        // returns true if everything was consummed AND there is more frames remaining
        bool consume_buffers(SAMPLE *& buf, int & nFrames) {
            A(consummed_frames < audioelement::n_frames_per_buffer);
            auto remaining_frames = audioelement::n_frames_per_buffer - consummed_frames;
            A(remaining_frames <= audioelement::n_frames_per_buffer);
            A(remaining_frames > 0);
            if(remaining_frames > nFrames) {
                // partial consume
                do_consume_buffers(buf, nFrames);
                consummed_frames += nFrames;
                A(consummed_frames < audioelement::n_frames_per_buffer);
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
            A(nFrames <= audioelement::n_frames_per_buffer); // by design
            A(consummed_frames < audioelement::n_frames_per_buffer); // by design
            
            memset(outputBuffer, 0, nFrames * nAudioOut * sizeof(SAMPLE));
            
            for( auto & c: channels ) {
                c.step(outputBuffer,
                       nFrames,
                       consummed_frames ); // with that, the channel knows when
                                           // the next computation of AudioElements will occur
            }

            for(int i=0; i<nFrames; ++i) {
                for(auto const & f: post_process) {
                    f(&outputBuffer[i*nAudioOut]); // or call the lambda for the whole buffer at once?
                }
            }

#if WITH_DELAY
            for( auto & delay : delays ) {
                // todo low pass filter for more realism
                delay.step(outputBuffer, nFrames);
            }
#endif
        }
    };
    
    using outputData = outputDataBase<PostProcess::COMPRESS>;
}
