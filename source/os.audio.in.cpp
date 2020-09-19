
using namespace imajuscule;
using namespace imajuscule::sensor;

void paTestData::step(const SAMPLE *rptr, int nFrames)
{
    LockGuard l(used);
    
    if( !activator.onStep() && rptr )
    {
        for( int i=0; i<nFrames; i++ )
        {
            auto val = *rptr++;
            algo_max.feed(val);
            
            // filter high frequencies
            avg.feed(val);
            auto filtered_value = avg.compute();
            algo_freq.feed(filtered_value);
        }
    }
    else {
        algo_max.forget();
        algo_freq.forget();
    }
}

bool AudioIn::Init()
{
#ifdef NO_AUDIO_IN
    Assert(0);
#else
    if(bInitialized_)
    {
        LG(WARN, "AudioIn::Init already initialized");
        return true;
    }
    
    data.algo_max.Register();
    data.algo_freq.Register();
    
    data.algo_max.setActivator(this);
    data.algo_freq.setActivator(this);

    bInitialized_ = true;
#endif
  return true;
}

bool AudioIn::do_wakeup() {
#ifdef NO_AUDIO_IN
  Assert(0);
  return false;
#else  // NO_AUDIO_IN
  LG(INFO, "AudioIn::do_wakeup : AudioIn will wake up");
  bool const res = audio_input.Init([this](const SAMPLE * buffer, int nFrames) {
    data.step(buffer, nFrames);
  }, SAMPLE_RATE);
  if (res) {
    LG(INFO, "AudioIn::do_wakeup : AudioIn is woken up");
  }
  return res;
#endif  // NO_AUDIO_IN
}



bool AudioIn::do_sleep() {
#ifdef NO_AUDIO_IN
  Assert(0);
  return false;
#else
  LG(INFO, "AudioIn::do_sleep : AudioIn will sleep");
  bool const res = audio_input.Teardown();
  if (res) {
    LG(INFO, "AudioIn::do_sleep : AudioIn sleeping");
  }
  return res;
#endif
}

void AudioIn::TearDown()
{
#ifdef NO_AUDIO_IN
    Assert(0);
#else
    Activator::sleep();

    if(bInitialized_) {
        data.algo_freq.Unregister();
        data.algo_max.Unregister();
        
        bInitialized_ = false;
    }
    else {
        LG(ERR, "AudioIn::TearDown : was not initialized");
    }
#endif
}

InternalResult AlgoMax::computeWhileLocked(float &f)
{
    f = maxAbsSinceLastRead;
    maxAbsSinceLastRead = 0.f;
    return InternalResult::COMPLETE_SUCCESS;
}
InternalResult AlgoMax::compute(float &f)
{
    Assert(0);
    return InternalResult::COMPLETE_ERROR;
}

static void plot(const char*title,const std::vector<SAMPLE>&v)
{
    return;
    LG(INFO,"%s", title);
    std::string s(v.size(), ' ');
    float min_ = 0.f;
    float max_ = 0.f;
    
    for(auto e:v)
    {
        min_ = std::min(min_, e);
        max_ = std::max(max_, e);
    }
    
    const int vSteps = 10;
    
    std::vector<std::string> vs(vSteps, s);
    int i=0;
    for(auto e:v)
    {
        auto normalized = (e - min_) / (max_ - min_);
        size_t index = (size_t)(normalized * (float)vSteps);
        if(index == vSteps)
            index = vSteps-1;
        vs[index][i] = '*';
        i++;
    }
    
    for(const auto & str:vs)
    {
        LG(INFO,"%s",str.c_str());
    }
    
}

InternalResult FreqFromZC::compute(float & f)
{
    Assert(0);
    return InternalResult::COMPLETE_ERROR;
}
InternalResult FreqFromZC::computeWhileLocked(float & f)
{
    auto delta = signal_range.delta();
    signal_range.set(0.f, 0.f);
    
    struct algo_freqFromZC
    {
        range<int32_t> periodRange;
        int32_t total = 0;
        int32_t count = 0;
        int32_t cur=0;
        bool hasStarted = false;
        std::function<void(int32_t)> step = [this](int32_t interval){
            if(!interval)
                return;
            
            if(!hasStarted)
            {
                if(periodRange.contains(interval))
                    hasStarted = true;
                else
                    return;
            }
            int32_t next = cur + interval;
            if(periodRange.contains(next))
            {
                total += next;
                count ++;
                cur = 0;
            }
            else if(periodRange.contains(next/2))
            {
                total += next;
                count += 2;
                cur = 0;
            }
            else if(next > periodRange.getMax() * 2)
                cur = 0;
            else
                cur = next;
        };
    } data;
    
    // use ranges : 200 202 203 201 302 99 should give the 200 range

    struct IntervalRange
    {
        IntervalRange(int32_t initiator) :
        initiator_(initiator)
        , count_(1){
            float jitter = ((float)initiator) * 0.15f;
            int32_t jitter_int = std::max(1, (int32_t)(jitter+0.5f));
            r_.set(initiator-jitter_int, initiator+jitter_int);
        }
        
        bool tryInsert(int32_t val)
        {
            if(r_.contains(val))
            {
                count_++;
                return true;
            }
            return false;
        }
        int32_t getCount() const { return count_;}
        int32_t getInitiator() const { return initiator_;}
        range<int32_t> const& getRange() const {return r_;}
    private:
        int32_t initiator_;
        range<int32_t> r_;
        int32_t count_;
    };
    StackVector<IntervalRange> ranges(positive_zeros_dist.size());
    
    auto it = positive_zeros_dist.begin();
    auto end = positive_zeros_dist.end();
    for(;it!=end;++it)
    {
        auto interval = *it;
        if(interval)
        {
            bool bFound = false;
            for(auto & r:ranges)
            {
                if( r.tryInsert(interval))
                {
                    bFound = true;
                    break;
                }
            }
            
            if(!bFound)
            {
                ranges.emplace_back(interval);
            }
        }
    }

    // ponderate frequency of occurences by initiator value so that 20 20 20 20 30 10 gives 20
    //      and so that 4 4 4 4 8 8 gives 8
    
    int32_t maxCount = 0;
    for(auto const& r:ranges)
    {
        auto count = r.getCount() * r.getInitiator();
        if(count > maxCount)
        {
            maxCount = count;
            data.periodRange = r.getRange();
        }
    }

    positive_zeros_dist.for_each(data.step);
    
    if(data.total)
    {
        // enable result if:
        // all periods were reconstructed from zero-crossing intervals
        // or the signal is loud enough to not be considered noise
        if(data.count == (int32_t)positive_zeros_dist.size() || delta > upperZero )
        {
            auto candidate = ((float)data.count) / ((float)data.total);
            candidate *= ((float)SAMPLE_RATE)/(float)sampling_period;
            if(candidate < minFreq )
            {
                //LG(INFO,"%.4f < %.4f : discarded", candidate, minFreq);
            }
            else if(candidate > maxFreq)
            {
                //LG(INFO,"%.4f > %.4f : discarded", candidate, maxFreq);
            }
            else
            {
                f = candidate;
                return InternalResult::COMPLETE_SUCCESS;
            }
        }
    }
    
    return InternalResult::COMPLETE_ERROR;
}
