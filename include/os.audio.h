

namespace imajuscule {
    
    class Audio {
        friend class Globals;
    public:
        static constexpr auto nAudioOut = AudioOut::nAudioOut;
        using Request = typename AudioOut::Request;

        static void Init();
        static void TearDown();
        static Audio * getInstance();
        
        AudioOut & out() { return audioOut; }
        
    private:
        Audio() = default;
        ~Audio() = default;
        static Audio * gInstance;
#ifndef NO_AUDIO_IN
        Sensor::AudioIn audioIn;
#endif
        AudioOut audioOut;
        
        void doInit();
        void doTearDown();
    };
}
