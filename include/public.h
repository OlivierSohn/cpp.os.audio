#pragma once

#include <algorithm>
#include <atomic>
#include <map>
#include <queue>
#include <type_traits>
#include <vector>

#ifndef NO_AUDIO_IN
# include "../../tp.muparser/include/public.h"
# include "../../cg.math/include/public.h"
#endif

#include "../../cpp.os.log/include/public.h"
#include "../../cpp.model/include/public.h"
#include "../../cpp.audio/include/public.h"

#if TARGET_OS_IOS
# import <AudioToolbox/AUComponent.h>
#endif

#include "os.audio.out.h"

#ifndef NO_AUDIO_IN
# include "os.audio.in.h"
#endif

#include "os.audio.h"
#include "instrument.h"
