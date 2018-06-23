

namespace imajuscule {
    class Audio;
  namespace audio {
    

    using outputData = outputDataBase<
        AudioOutPolicy::MasterGlobalLock,
        ChannelsVecAggregate<2, AudioOutPolicy::MasterGlobalLock>
        >;
    
    struct AudioOut : public NonCopyable {

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
        using XFadeChans         = typename outputData::ChannelsT::XFadeChans;
        using NoXFadeChans       = typename outputData::ChannelsT::NoXFadeChans;
        using XFadeInfiniteChans = typename outputData::ChannelsT::XFadeInfiniteChans;
        using Volumes = AudioCtxt::Volumes;
        static constexpr auto atomicity = outputData::ChannelsT::atomicity;

        friend class Audio;

    private:
        AudioCtxt ctxt{
            GlobalAudioLock<AudioOutPolicy::MasterGlobalLock>::get()
        };
        
        Sounds<atomicity> sounds;

    public:
        using Request = AudioCtxt::Request;
        static constexpr auto nAudioOut = AudioCtxt::nAudioOut;
      
      auto & getChannelHandler() { return ctxt.getChannelHandler(); }

      AudioOut() {
        auto p = std::make_unique<XFadeInfiniteChans>(
                                                      getChannelHandler().get_lock_policy(),
                                                      std::numeric_limits<uint8_t>::max(),
                                                      n_max_orchestrators_per_channel);
        {
          AudioCtxt::LockFromNRT l(getChannelHandler().get_lock());
          
          getChannelHandler().getChannels().getChannelsXFadeInfinite().emplace_back(std::move(p));
        }
        
        auto p2 = std::make_unique<XFadeChans>(
                                               getChannelHandler().get_lock_policy(),
                                               std::numeric_limits<uint8_t>::max(),
                                               n_max_orchestrators_per_channel);
        {
          AudioCtxt::LockFromNRT l(getChannelHandler().get_lock());
          
          getChannelHandler().getChannels().getChannelsXFade().emplace_back(std::move(p2));
        }

        auto p3 = std::make_unique<NoXFadeChans>(
                                               getChannelHandler().get_lock_policy(),
                                               std::numeric_limits<uint8_t>::max(),
                                               n_max_orchestrators_per_channel);
        {
          AudioCtxt::LockFromNRT l(getChannelHandler().get_lock());
          
          getChannelHandler().getChannels().getChannelsNoXFade().emplace_back(std::move(p3));
        }
      }

        ~AudioOut() {
            ctxt.finalize(); // needs to be called before 'Sounds' destructor
        }
      
        void Init(float minOutputLatency) {
          ctxt.Init(minOutputLatency);
        }
        void initializeConvolutionReverb() { ctxt.initializeConvolutionReverb(); }
        void TearDown() { ctxt.TearDown(); }

        auto & getCtxt() { return ctxt; }
      
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

        template<typename F>
        void playComputable( uint8_t channel_id, F compute, Request && req) {
          return ctxt.playComputable( channel_id, compute, std::move(req));
        }

        void toVolume( uint8_t channel_id, float volume, int nSteps ) {
            ctxt.toVolume( channel_id, volume, nSteps);
        }

        void closeChannel(uint8_t channel_id, CloseMode mode) {
            ctxt.closeChannel( channel_id, mode );
        }

        Sounds<atomicity> & editSounds() { return sounds; }

    };
  }
}
