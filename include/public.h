#pragma once

#include <algorithm>
#include <atomic>
#include <complex>
#include <map>
#include <queue>
#include <type_traits>
#include <vector>

#include "../../algorithms/include/public.h"
#include "../../tp.muparser/include/public.h"
#include "../../os.log/include/public.h"
#include "../../os.storage/include/public.h"
#include "../../model/include/public.h"
#include "../../wiremodeler/include/public.h"
#include "../../cg.math/include/public.h"
#include "../../audio/include/public.h"

#if TARGET_OS_IOS
# import <AudioToolbox/AUComponent.h>
#endif

#include "os.audio.out.h"

#ifndef NO_AUDIO_IN
# include "os.audio.in.h"
#endif

#include "os.audio.h"
#include "instrument.h"
