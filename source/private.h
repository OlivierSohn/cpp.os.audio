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

// fft method is not easy to implement, i prefered zero crossing instead but i leave it here just in case
//#include "kiss_fftr.h"
