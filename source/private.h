#include "public.h"

#define  _USE_MATH_DEFINES
#include <cmath>
#include <stdlib.h>
#include <string.h>
#include <cerrno>
#include <functional>
#include <map>
#include <cstdlib>

#if TARGET_OS_IOS
#import <AudioToolbox/AudioToolbox.h>
#else
// SDL does not have audio recording yet so I use portaudio
//#include "SDL_audio.h"
#include "portaudio.h"
#endif

#if TARGET_OS_IOS
struct iOSOutputData {
    // we cannot know for sure how much the os will ask us to compute.
    // on my iPhone 4s I observed 512 was asked.
    static constexpr auto initial_buffer_size = 1024;
    
    iOSOutputData() {
        // preallocate to avoid dynamic allocation in audio thread
        buf.reserve(initial_buffer_size);
    }
    
    outputData * data = nullptr;
    std::vector<outputData::T> buf;
};

int initAudioSession();
int initAudioStreams(AudioUnit & audioUnit, void * data, AURenderCallback cb, int nOuts);
int initAudioStreams(AudioUnit & audioUnit, imajuscule::Sensor::paTestData & data, AURenderCallback cb, int nOuts);
int initAudioStreams(AudioUnit & audioUnit, outputData & data, AURenderCallback cb, int nOuts);

OSStatus startAudioUnit(AudioUnit audioUnit);
OSStatus stopProcessingAudio(AudioUnit audioUnit);

#endif
