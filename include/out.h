
namespace imajuscule {
   
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
