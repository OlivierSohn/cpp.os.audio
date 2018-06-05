

namespace imajuscule {
    class Audio;
  namespace audio {

    using outputData = outputDataBase<
        AudioOutPolicy::Master,
      Channels<2, XfadePolicy::UseXfade, MaxQueueSize::Infinite, AudioOutPolicy::Master>
        >;
    
    class AudioOut : public NonCopyable {

        static constexpr auto n_max_orchestrators_per_channel = 1;
        
        static constexpr auto AudioPlat =
#if TARGET_OS_IOS
            audio::AudioPlatform::AudioUnits;
#else
            audio::AudioPlatform::PortAudio;
#endif

        static constexpr auto WithAudioIn =
#ifdef NO_AUDIO_IN
            audio::Features::JustOut;
#else
            audio::Features::InAndOut;
#endif

        using AudioCtxt = audio::AudioOutContext<outputData,WithAudioIn,AudioPlat>;
        using Volumes = AudioCtxt::Volumes;

        friend class Audio;

        AudioCtxt ctxt{
            GlobalAudioLock<AudioOutPolicy::Master>::get(),
            std::numeric_limits<uint8_t>::max(),
            n_max_orchestrators_per_channel
        };
        
        Sounds sounds;

    public:
        using Request = AudioCtxt::Request;
        static constexpr auto nAudioOut = AudioCtxt::nAudioOut;

        ~AudioOut() {
            ctxt.finalize(); // needs to be called before 'Sounds' destructor
        }

        void Init() { ctxt.Init(); }
        void initializeConvolutionReverb() { ctxt.initializeConvolutionReverb(); }
        void TearDown() { ctxt.TearDown(); }

        auto & getCtxt() { return ctxt; }
        auto & getChannelHandler() { return ctxt.getChannelHandler(); }
        
        void onApplicationShouldClose() {
            ctxt.onApplicationShouldClose();
        }

        bool Initialized() const { return ctxt.Initialized(); }

        uint8_t openChannel(float volume = 1.f,
                            ChannelClosingPolicy p = ChannelClosingPolicy::ExplicitClose,
                            int xfade_length = 401) {
            return ctxt.openChannel(volume, p, xfade_length);
        }

        void play( uint8_t channel_id, StackVector<Request> && v ) {
            return ctxt.play(channel_id, std::move(v));
        }

        template<class ...Args>
        void playGeneric( uint8_t channel_id, Args&& ...args ) {
            return ctxt.playGeneric( channel_id, std::forward<Args>( args )... );
        }

        void toVolume( uint8_t channel_id, float volume, int nSteps ) {
            ctxt.toVolume( channel_id, volume, nSteps);
        }

        void closeChannel(uint8_t channel_id, CloseMode mode) {
            ctxt.closeChannel( channel_id, mode );
        }

        Sounds & editSounds() { return sounds; }

    };
  }
}
