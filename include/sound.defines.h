# define IMJ_PORTAUDIO_SAMPLE_TYPE  paFloat32
using SAMPLE = float;

typedef void PaStream; //from "portaudio.h"

namespace imajuscule {
    
    constexpr int SAMPLE_RATE = 44100;

    
    constexpr int ms_to_frames(float duration_ms) {
        return static_cast<int>( SAMPLE_RATE * 0.001f * duration_ms );
    }
    
    constexpr float frames_to_ms(float duration_frames) {
        return 1000.f * duration_frames / SAMPLE_RATE;
    }
    
    constexpr float int_period_to_freq(int period) {
        return SAMPLE_RATE / static_cast<float>(period);
    }
    
    constexpr int freq_to_int_period( float freq_hz ) {
        if(freq_hz <= 0.f) {
            return 1;
        }
        return static_cast<int>(SAMPLE_RATE / freq_hz);
    }
    
    
    constexpr unsigned int nAudioOut = 2; // on some systems, if there is only one output, portaudio may refuse to open the stream
}
