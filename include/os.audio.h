

namespace imajuscule {

    class Audio {
        friend class Globals;
      
    public:
        static constexpr auto nAudioOut = audio::AudioOut::nAudioOut;
        using Request = typename audio::AudioOut::Request;

        enum class OutInitPolicy {
            LAZY, // in this mode audio out is initialized upon first call to AudioOut::openChannel
            FORCE
        };
        [[nodiscard]] static bool Init(OutInitPolicy);

        static void TearDown();
        static Audio * getInstance();

        audio::AudioOut & out() { return audioOut; }

    private:
        Audio() = default;
        ~Audio() = default;
        static Audio * gInstance;

        #ifndef NO_AUDIO_IN
        sensor::AudioIn audioIn;
        #endif

        audio::AudioOut audioOut;

        bool doInit(OutInitPolicy);
        void doTearDown();
    };
}
