
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

outputData::outputData() : delays{{1000, 0.6f},{4000, 0.2f}, {4300, 0.3f}, {5000, 0.1f}} {
    // to avoid reallocations when we hold the lock
    // we allocate all we need for channel management now:
    channels.reserve(std::numeric_limits<uint8_t>::max());
    autoclosing_ids.reserve(std::numeric_limits<uint8_t>::max());
    available_ids.reserve(std::numeric_limits<uint8_t>::max());
}

uint8_t outputData::openChannel(channelVolumes volume, ChannelClosingPolicy l) {
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
    A(id != AUDIO_CHANNEL_NONE);
    return id;
}

bool outputData::closeChannel(uint8_t channel_id) {
    // we could
    // - transform the channel into an autoclosing channel
    // - clear the queue (except the first element in case it is crossfading)
    // to stop playing "as soon as possible but without generating undesirable clics"
    {
        RAIILock l(used);
        editChannel(channel_id).clear();
    }
    available_ids.Return(channel_id);
    return channels.empty();
}

void outputData::play( uint8_t channel_id, StaticVector<Request> && v ) {
    RAIILock l(used);
    
    auto & c = editChannel(channel_id);
    for( auto & sound : v ) {
        c.addRequest( std::move(sound) );
    }
}

void outputData::setVolume(uint8_t channel_id, channelVolumes volumes) {
    auto & c = editChannel(channel_id);
    c.volume_transition_remaining = Channel::volume_transition_length;
    for(int i=0; i<nAudioOut; ++i) {
        c.volumes[i].increments = (volumes[i] - c.volumes[i].current) / (float) Channel::volume_transition_length;
    }
}

void outputData::step(SAMPLE *outputBuffer, int nFrames) {
    
    memset(outputBuffer,0,nFrames*nAudioOut*sizeof(SAMPLE));

    {
        RAIILock l(used);
        
        for( auto & c: channels ) {
            c.step( outputBuffer, nFrames );
        }
    }
    
    // apply the effect
    for( auto & delay : delays ) {
        // deactivated on purpose : reactivate once low pass filtered
        //delay.step(outputBuffer, nFrames);
    }
}

void Channel::step(SAMPLE * outputBuffer, int n_max_writes)
{
    if(remaining_samples_count == 0 && !consume()) {
        return;
    }
    while(1)
    {
        while(remaining_samples_count < n_max_writes)
        {
            {
                auto xfade_written = crossfading_from_zero_remaining();
                if(xfade_written > 0) {
                    auto xfade_ratio = 0.5f * (float)(xfade_written-1) * inv_half_size_xfade;
                    xfade_written = std::min(xfade_written, remaining_samples_count);
                    write_xfade_right( outputBuffer, xfade_ratio, xfade_written );
                    remaining_samples_count -= xfade_written;
                    if(remaining_samples_count == 0 && !consume()) {
                        return;
                    }
                    n_max_writes -= xfade_written;
                    A(n_max_writes > 0);
                    outputBuffer += xfade_written * nAudioOut;
                    A(crossfading_from_zero_remaining() <= 0);
                }
            }
            A(remaining_samples_count >= 0);
            {
                auto remaining_normal = remaining_samples_count - size_half_xfade - 1;
                if(remaining_normal > 0) {
                    write( outputBuffer, remaining_normal );
                    n_max_writes -= remaining_normal;
                    remaining_samples_count -= remaining_normal;
                    if(n_max_writes <= 0) {
                        return;
                    }
                    outputBuffer += remaining_normal * nAudioOut;
                }
            }
            A(remaining_samples_count >= 0);
            A(n_max_writes > 0);
            A(remaining_samples_count <= size_half_xfade + 1);
            {
                if(!handleToZero(outputBuffer, n_max_writes)) {
                    return;
                }
                continue;
            }
            
            A(n_max_writes > 0);
            if (!consume()) {
                return;
            }
        }
        {
            auto xfade_written = crossfading_from_zero_remaining();
            if(xfade_written > 0) {
                auto xfade_ratio = 0.5f * (float)(xfade_written-1) * inv_half_size_xfade;
                xfade_written = std::min(xfade_written, n_max_writes);
                write_xfade_right( outputBuffer, xfade_ratio, xfade_written );
                remaining_samples_count -= xfade_written;
                if(remaining_samples_count == 0 && !consume()) {
                    return;
                }
                n_max_writes -= xfade_written;
                if(n_max_writes <= 0) {
                    return;
                }
                outputBuffer += xfade_written * nAudioOut;
                A(crossfading_from_zero_remaining() <= 0); // we are sure the xfade is finished
                
                if(remaining_samples_count < n_max_writes) {
                    continue;
                }
            }
        }
        {
            auto remaining_normal = remaining_samples_count - size_half_xfade - 1;
            if(remaining_normal > 0) {
                if(remaining_normal <= n_max_writes) {
                    write( outputBuffer, remaining_normal );
                    n_max_writes -= remaining_normal;
                    remaining_samples_count -= remaining_normal;
                    if(n_max_writes <= 0) {
                        return;
                    }
                    outputBuffer += remaining_normal * nAudioOut;
                }
                else {
                    write( outputBuffer, n_max_writes );
                    remaining_samples_count -= n_max_writes;
                    return;
                }
            }
        }
        A(remaining_samples_count >= 0);
        if(remaining_samples_count <= size_half_xfade + 1) {
            if(!handleToZero(outputBuffer, n_max_writes)) {
                return;
            }
            continue;
        }
        return;
    }
}

void Channel::write(SAMPLE * outputBuffer, int const n_writes) {
    A(n_writes > 0);
//    LG(INFO, "write %d", n_writes);
    auto const s = (int) current.buffer->size();
    auto const a = base_amplitude * current.volume;
    for( int i=0; i<n_writes; ++i) {
        if( current_next_sample_index == s ) {
            current_next_sample_index = 0;
        }
        A(current_next_sample_index < s);
        
        A(crossfading_from_zero_remaining() <= 0);
        auto val = a * (*current.buffer)[current_next_sample_index];
        if( volume_transition_remaining ) {
            volume_transition_remaining--;
            for(auto i=0; i<nAudioOut; ++i) {
                volumes[i].current += volumes[i].increments;
            }
        }
        for(auto i=0; i<nAudioOut; ++i) {
            *outputBuffer += val * volumes[i].current;
            ++outputBuffer;
        }
        ++current_next_sample_index;
    }
}

void Channel::write_xfade_right(SAMPLE * outputBuffer, float xfade_ratio, int const n_writes) {
    A(n_writes > 0);
//    LG(INFO, ">>>>> %d", n_writes);
    A(n_writes <= crossfading_from_zero_remaining());
    A(xfade_ratio >= 0.f);
    A(xfade_ratio <= 1.f);

    // note that next points probably to deallocated memory but it's ok we don't dereference it
    auto const * other_buffer = (next || !current.buffer) ? previous.buffer : nullptr; // only end crossfade with other if we started with him
    int const other_s = other_buffer? (int)other_buffer->size() : 0;
    int const s = current.buffer ? (int) current.buffer->size() : 0;
    for( int i=0; i<n_writes; i++ ) {
        auto val = 0.f;
        if(s) {
            if( current_next_sample_index == s ) {
                current_next_sample_index = 0;
            }
            A(current_next_sample_index < s);
            val = (1.f - xfade_ratio) * current.volume * (*current.buffer)[current_next_sample_index];
            ++current_next_sample_index;
        }
        if(other_s) {
            A(other_next_sample_index >= 0);
            A(other_next_sample_index <= other_s);
            if(other_next_sample_index == other_s) {
                other_next_sample_index = 0;
            }
            A(other_next_sample_index <= other_s);
            val += xfade_ratio * previous.volume * (*previous.buffer)[other_next_sample_index];
            ++other_next_sample_index;
        }
        xfade_ratio -= xfade_increment;
        write_value(val, outputBuffer);
    }
}


void Channel::write_xfade_left(SAMPLE * outputBuffer, float xfade_ratio, int const n_writes) {
    A(n_writes > 0);
//    LG(INFO, "<<<<< %d", n_writes);
    A(n_writes <= remaining_samples_count);
    A(xfade_ratio >= 0.f);
    A(xfade_ratio <= 1.f);
    auto * other = next ? &requests.front() : nullptr;
    int const other_s = other? safe_cast<int>(other->buffer->size()) : 0;
    int const s = current.buffer ? (int) current.buffer->size() : 0;
    for( int i=0; i<n_writes; i++ ) {
        auto val = 0.f;
        if(s) {
            if( current_next_sample_index == s ) {
                current_next_sample_index = 0;
            }
            A(current_next_sample_index < s);
            val = xfade_ratio * current.volume * (*current.buffer)[current_next_sample_index];
            ++current_next_sample_index;
        }
        if(other_s) {
            A(other_next_sample_index >= 0);
            A(other_next_sample_index <= other_s);
            if(other_next_sample_index == other_s) {
                other_next_sample_index = 0;
            }
            A(other_next_sample_index <= other_s);
            val += (1.f - xfade_ratio) * other->volume * (*other->buffer)[other_next_sample_index];
            ++other_next_sample_index;
        }
        xfade_ratio -= xfade_increment;
        write_value(val, outputBuffer);
    }
}
