# define IMJ_PORTAUDIO_SAMPLE_TYPE  paFloat32
using SAMPLE = float;

typedef void PaStream; //from "portaudio.h"

namespace imajuscule {
    
    constexpr int SAMPLE_RATE = 44100;
    
    constexpr unsigned int nAudioOut = 2; // on some systems, if there is only one output, portaudio may refuse to open the stream
}
