#include "../include/public.h"

#define  _USE_MATH_DEFINES
#include <cmath>
#include <stdlib.h>
#include <string.h>
#include <cerrno>
#include <functional>
#include <map>
#include <cstdlib>

#if TARGET_OS_IOS
int initAudioStreams(AudioUnit & audioUnit, void * data, AURenderCallback cb, int nOuts, AudioStreamBasicDescription & streamDescription);
#ifndef NO_AUDIO_IN
int initAudioStreams(AudioUnit & audioUnit, imajuscule::sensor::paTestData & data, AURenderCallback cb, int nOuts, AudioStreamBasicDescription & streamDescription);
#endif
int initAudioStreams(AudioUnit & audioUnit, imajuscule::audio::outputData & data, AURenderCallback cb, int nOuts, AudioStreamBasicDescription & streamDescription);

#endif
