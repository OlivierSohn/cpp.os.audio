namespace imajuscule {
    
    struct StereoGain {
        float left, right;
        
        StereoGain opposite() const { return { right, left }; }
    };
    
    static inline StereoGain stereo(float pan) {
        pan = std::min(pan, 1.f);
        pan = std::max(pan, -1.f);
        
        // http://dsp.stackexchange.com/questions/21691/algorithm-to-pan-audio
        auto angle = 0.25f * (pan + 1.f) * static_cast<float>(M_PI);
        
        return {cosf(angle), sinf(angle)};
    }
    
}
