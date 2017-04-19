

namespace imajuscule {
    namespace audio {
        constexpr auto initial_n_audio_cb_frames = -1;
        
        int wait_for_first_n_audio_cb_frames();
        
        template<typename OutputData>
        void useConvolutionReverb(OutputData & data,
                                  std::string const & dirname, std::string const & filename) {
            
            resource rsrc;
            auto found = findResource(filename, dirname, rsrc);
            if(!found) {
                LG(WARN, "impulse response not found");
                return;
            }
            WAVReader reader(rsrc.first, rsrc.second);
            
            auto res = reader.Initialize();
            
            A(ILE_SUCCESS == res);
            
            FFT_T stride = reader.getSampleRate() / static_cast<float>(SAMPLE_RATE);
            //FFT_T stride = 1.f;
            std::vector<FFT_T> buf(static_cast<int>(reader.countFrames() / stride) * reader.countChannels());
            MultiChannelDownSampling<decltype(reader)> mci(reader);
            mci.Read(buf.begin(), buf.end(), stride);
            buf.resize(std::distance(buf.begin(), buf.end()));
            
            data.setConvolutionReverbIR(std::move(buf),
                                        reader.countChannels(),
                                        wait_for_first_n_audio_cb_frames());
        }
    }

    class Audio;
    
    class AudioOut : public NonCopyable {
        
        static constexpr auto xfade_on_close = 200;
        static constexpr auto n_max_orchestrators_per_channel = 1;
        
        // members
        
        PaStream *stream = nullptr;
        bool bInitialized : 1;
        bool closing : 1;
        outputData data{
            std::numeric_limits<uint8_t>::max(),
            n_max_orchestrators_per_channel
        };
        Sounds sounds;

        // methods
    private:
        friend class Audio;

        AudioOut() : bInitialized(false), closing(false) {}
        
        ~AudioOut() {
            data.closeAllChannels(0); // needs to be called before 'Sounds' destructor
        }
        
        void Init();
        bool doInit();
        void initializeConvolutionReverb();
        void TearDown();

    public:
        using Volumes = decltype(data)::Volumes;
        static constexpr auto nAudioOut = decltype(data)::nOuts;
        using Request = decltype(data)::Request;

        auto & getChannelHandler() { return data; }
        void onApplicationShouldClose() {
            if(closing) {
                return;
            }
            closing = true;
            data.closeAllChannels(xfade_on_close);
            LG(INFO, "Fading out Audio before shutdown...");
        }
        
        bool Initialized() const { return bInitialized; }
        uint8_t openChannel(float volume = 1.f,
                            ChannelClosingPolicy p = ChannelClosingPolicy::ExplicitClose,
                            int xfade_length = 401) {
            if(closing) {
                return AUDIO_CHANNEL_NONE;
            }
            Init();
            return data.template openChannel<WithLock::Yes>(volume, p, xfade_length);
        }

        void play( uint8_t channel_id, StackVector<Request> && v ) {
            if(closing) {
                return;
            }
            data.play( channel_id, std::move( v ) );
        }
        
        template<class ...Args>
        void playGeneric( uint8_t channel_id, Args&& ...args ) {
            if(closing) {
                return;
            }
            data.playGeneric( channel_id, std::forward<Args>( args )... );
        }
        
        void toVolume( uint8_t channel_id, float volume, int nSteps ) {
            if(closing) {
                return;
            }
            data.toVolume( channel_id, volume, nSteps);
        }

        void closeChannel(uint8_t channel_id, CloseMode mode) {
            if(closing) {
                return;
            }
            data.closeChannel( channel_id, mode );
        }
        
        int get_xfade_millis(uint8_t channel_id) const {
            return data.getChannel(channel_id).duration_millis_xfade();
        }
        
        Sounds & editSounds() { return sounds; }
    };

}
