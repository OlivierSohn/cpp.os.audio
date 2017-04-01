

namespace imajuscule {
    
    class Audio {
        friend class Globals;
    public:
        static constexpr auto nAudioOut = AudioOut::nAudioOut;
        using Request = typename AudioOut::Request;

        enum class OutInitPolicy {
            LAZY, // in this mode audio out is initialized upon first call to AudioOut::openChannel
            FORCE
        };
        static void Init(OutInitPolicy);

        static void TearDown();
        static Audio * getInstance();
        
        AudioOut & out() { return audioOut; }
        
    private:
        Audio() = default;
        ~Audio() = default;
        static Audio * gInstance;
        Sensor::AudioIn audioIn;
        AudioOut audioOut;
        
        void doInit(OutInitPolicy);
        void doTearDown();
    };
}
