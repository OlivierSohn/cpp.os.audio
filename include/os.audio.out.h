

namespace imajuscule {
    class Audio;
  namespace audio {
    

    // These things are missing to be able to use 'MasterLockFree':
    //   - never read the buffer state from a non rt thread (see editInactiveAudioElement) :
    //      it is meaningless, because the state is changed by the audio thread.

    using outputData = outputDataBase<
        ChannelsVecAggregate<2, AudioOutPolicy::MasterGlobalLock>,
        ReverbType::Realtime_Synchronous
        >;
    
    struct AudioOut : public NonCopyable {

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
        AudioCtxt ctxt;
        
        Sounds<atomicity> sounds;

    public:
        using Request = AudioCtxt::Request;
        static constexpr auto nAudioOut = AudioCtxt::nAudioOut;
      
      auto & getChannelHandler() { return ctxt.getChannelHandler(); }

      AudioOut()
      : ctxt()
      {
        getChannelHandler().getChannels().getChannelsXFadeInfinite().emplace_front(getChannelHandler().get_lock_policy(),
                                                                                   std::numeric_limits<uint8_t>::max());
        
        getChannelHandler().getChannels().getChannelsXFade().emplace_front(getChannelHandler().get_lock_policy(),
                                                                           std::numeric_limits<uint8_t>::max());

        getChannelHandler().getChannels().getChannelsNoXFade().emplace_front(getChannelHandler().get_lock_policy(),
                                                                             std::numeric_limits<uint8_t>::max());
      }

        ~AudioOut() {
            ctxt.finalize(); // needs to be called before 'Sounds' destructor
        }
      
      std::optional<int> getSampleRate() const {
        return ctxt.getSampleRate();
      }
      
        [[nodiscard]] bool Init(int sample_rate, float minOutputLatency) {
          return ctxt.Init(sample_rate, minOutputLatency);
        }
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

        bool play( uint8_t channel_id, StackVector<Request> && v ) {
            return ctxt.play(channel_id, std::move(v));
        }

        template<typename Algo>
        [[nodiscard]] bool playComputable(PackedRequestParams<nAudioOut> params,
                                          audioelement::FinalAudioElement<Algo> & e) {
          return ctxt.playComputable(params, e);
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
