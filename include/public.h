#pragma once

#include <algorithm>
#include <atomic>
#include <complex>
#include <map>
#include <queue>
#include <type_traits>
#include <vector>

#include "../../cpp.algorithms/include/public.h"
#include "../../tp.muparser/include/public.h"
#include "../../cpp.os.logs/include/public.h"
#include "../../cpp.os.storage/include/public.h"
#include "../../cpp.model/include/public.h"
#include "../../wiremodeler/include/public.h"
#include "../../cg.math/include/public.h"
#include "../../cpp.audio/include/public.h"

#if TARGET_OS_IOS
# import <AudioToolbox/AUComponent.h>
#endif

#include "os.audio.out.h"
#include "os.audio.in.h"
#include "os.audio.h"
#include "instrument.h"
