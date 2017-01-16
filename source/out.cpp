
using namespace imajuscule;

DelayLine::DelayLine(int size, float attenuation): delay(size,{{}}), it(0), end(size), attenuation(attenuation) {}

void DelayLine::step(SAMPLE *outputBuffer, int nFrames) {
    for( int i=0; i < nFrames; i++ ) {
        auto & d = delay[it];
        for(auto j=0; j<nAudioOut; ++j) {
            auto delayed = d[j];
            d[j] = *outputBuffer;
            *outputBuffer += attenuation * delayed;
            ++outputBuffer;
        }
        ++it;
        if( unlikely(it == end) ) {
            it = 0;
        }
    }
}

constexpr auto osc_freq = 300.f;
constexpr auto ramp_duration_seconds = 1.f;

outputData::outputData()
: delays{{1000, 0.6f},{4000, 0.2f}, {4300, 0.3f}, {5000, 0.1f}},
clock_(false),
consummed_frames(0),
ramp(300.f,
     600.f,
     ramp_duration_seconds * SAMPLE_RATE,
     itp::EASE_INOUT_QUINT),
osc(angle_increment_from_freq(osc_freq)){
    // to avoid reallocations when we hold the lock
    // we allocate all we need for channel management now:
    channels.reserve(std::numeric_limits<uint8_t>::max());
    autoclosing_ids.reserve(std::numeric_limits<uint8_t>::max());
    available_ids.reserve(std::numeric_limits<uint8_t>::max());
    
}

uint8_t outputData::openChannel(channelVolumes volume, ChannelClosingPolicy l, int xfade_length) {
    uint8_t id = AUDIO_CHANNEL_NONE;
    if(channels.size() == std::numeric_limits<uint8_t>::max() && available_ids.size() == 0) {
        // Channels are at their maximum number and all are used...
        // Let's find one that is autoclosing and not playing :
        for( auto it = autoclosing_ids.begin(), end = autoclosing_ids.end(); it != end; ++it )
        {
            id = *it;
            {
                // take the lock in the loop so that at the end of each iteration
                // the audio thread has a chance to lock
                RAIILock l(used);
                if(channels[id].isPlaying()) {
                    continue;
                }
            }
            // channel 'id' is auto closing and not playing, so we will assign it to the caller.
            if(l != AutoClose) {
                autoclosing_ids.erase(it);
            }
            break;
        }
    }
    else {
        id = available_ids.Take(channels);
        if(l == AutoClose) {
            autoclosing_ids.push_back(id);
            A(autoclosing_ids.size() <= std::numeric_limits<uint8_t>::max());
            // else logic error : some users closed manually some autoclosing channels
        }
    }
    // no need to lock here : the channel is not active
    for(auto i=0; i<nAudioOut; ++i) {
        editChannel(id).volumes[i].current = volume[i];
    }
    editChannel(id).set_xfade(xfade_length);
    A(id != AUDIO_CHANNEL_NONE);
    return id;
}

bool outputData::closeChannel(uint8_t channel_id) {
    {
        RAIILock l(used);
        editChannel(channel_id).stopPlaying();
    }
    available_ids.Return(channel_id);
    return channels.empty();
}

void outputData::play( uint8_t channel_id, StackVector<Request> && v ) {
    RAIILock l(used);
    
    auto & c = editChannel(channel_id);
    for( auto & sound : v ) {
        c.addRequest( std::move(sound) );
    }
}

void outputData::setVolume(uint8_t channel_id, channelVolumes volumes) {
    // no need to lock
    auto & c = editChannel(channel_id);
    c.volume_transition_remaining = Channel::volume_transition_length;
    for(int i=0; i<nAudioOut; ++i) {
        c.volumes[i].increments = (volumes[i] - c.volumes[i].current) / (float) Channel::volume_transition_length;
    }
}

void outputData::step(SAMPLE *outputBuffer, int nFrames) {
    // nFrames might vary, if we want to monitor that:
    /*static int sNFrames = -1;
    if(nFrames != sNFrames) {
        LG(INFO, "frames changed: %d -> %d", sNFrames, nFrames);
        sNFrames = nFrames;
    }*/
    
    {
        RAIILock l(used);

        if(consummed_frames != 0) {
            // finish consuming previous buffers
            if(!consume_buffers(outputBuffer, nFrames)) {
                return;
            }
        }

        while(true) {
            // the previous buffers are consumed, we need to compute them again
            computeNextAudioElementsBuffers();
            
            if(!consume_buffers(outputBuffer, nFrames)) {
                return;
            }
        }
    }

    /*
    auto b = outputBuffer;
    for(auto i = 0; i < nFrames; ++i) {
        stepOscillators();
        for(int i=0; i<nAudioOut; ++i) {
            if(i==0) {
                *b += 0.1f * osc.real();
            }
            if(i==1) {
                *b += 0.1f * ramp.real();
            }
            ++b;
        }
    }*/
    
}

