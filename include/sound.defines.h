# define IMJ_PORTAUDIO_SAMPLE_TYPE  paFloat32
using SAMPLE = float;

typedef void PaStream; //from "portaudio.h"

namespace imajuscule {
    
    constexpr int SAMPLE_RATE = 44100;
    
    constexpr float int_period_to_freq(int period) {
        return SAMPLE_RATE / static_cast<float>(period);
    }
    
    constexpr int freq_to_int_period( float freq_hz ) {
        if(freq_hz <= 0.f) {
            return 1;
        }
        return (int) (((float)SAMPLE_RATE) / freq_hz);
    }
    
}
