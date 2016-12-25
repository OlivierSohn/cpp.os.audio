
using namespace imajuscule;

DelayLine::DelayLine(int size, float attenuation): delay(size,0.f), it(0), end(size), attenuation(attenuation) {}

void DelayLine::step(SAMPLE *outputBuffer, int framesPerBuffer) {
    for( int i=0; i < framesPerBuffer; i++ ) {
        auto & d = delay[it];
        auto delayed = d;
        d = outputBuffer[i];
        outputBuffer[i] += attenuation * delayed;
        ++it;
        if( unlikely(it == end) ) {
            it = 0;
        }
    }
}

outputData::outputData() : delays{{1000, 0.6f},{4000, 0.2f}, {4300, 0.3f}, {5000, 0.1f}} {
}

uint8_t outputData::openChannel(float volume) {
    RAIILock l(used);
    
    auto index = available_ids.Take(channels);
    A(index != AUDIO_CHANNEL_NONE); // now channel vector is full!
    editChannel(index).channel_volume = volume;
    
    return index;
}

bool outputData::closeChannel(uint8_t channel_id) {
    RAIILock l(used);
    
    editChannel(channel_id).clear();
    available_ids.Return(channel_id);
    return channels.empty();
}

void outputData::play( uint8_t channel_id, pool::vector<Request> && v ) {
    RAIILock l(used);
    
    auto & c = editChannel(channel_id);
    for( auto & sound : v ) {
        c.addRequest( std::move(sound) );
    }
}

void outputData::setVolume(uint8_t channel_id, float vol) {
    auto & c = editChannel(channel_id);
    c.transition_volume_remaining = Channel::volume_transition_length;
    c.channel_volume_increments = (vol - c.channel_volume) / (float) Channel::volume_transition_length;
}

void outputData::step(SAMPLE *outputBuffer, int framesPerBuffer) {
    
    memset(outputBuffer,0,framesPerBuffer*sizeof(SAMPLE));

    {
        RAIILock l(used);
        
        for( auto & c: channels ) {
            c.step( outputBuffer, framesPerBuffer );
        }
    }
    
    // apply the effect
    for( auto & delay : delays ) {
        // deactivated on purpose : reactivate once low pass filtered
        //delay.step(outputBuffer, framesPerBuffer);
    }
}

void outputData::Channel::step(SAMPLE * outputBuffer, int n_max_writes)
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
                    outputBuffer += xfade_written;
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
                    outputBuffer += remaining_normal;
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
                outputBuffer += xfade_written;
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
                    outputBuffer += remaining_normal;
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

void outputData::Channel::write(SAMPLE * outputBuffer, int const n_writes) {
    A(n_writes > 0);
//    LG(INFO, "write %d", n_writes);
    auto const s = (int) current.buffer->values.size();
    auto const a = amplitude * current.volume;
    for( int i=0; i<n_writes; i++ ) {
        if( current_next_sample_index == s ) {
            current_next_sample_index = 0;
        }
        A(current_next_sample_index < s);
        
        if( transition_volume_remaining ) {
            transition_volume_remaining--;
            channel_volume += channel_volume_increments;
        }
        A(crossfading_from_zero_remaining() <= 0);
        *outputBuffer += a * channel_volume * current.buffer->values[current_next_sample_index];
        ++outputBuffer;
        ++current_next_sample_index;
    }
}

void outputData::Channel::write_xfade_right(SAMPLE * outputBuffer, float xfade_ratio, int const n_writes) {
    A(n_writes > 0);
//    LG(INFO, ">>>>> %d", n_writes);
    A(n_writes <= crossfading_from_zero_remaining());
    A(xfade_ratio >= 0.f);
    A(xfade_ratio <= 1.f);

    // note that next points probably to deallocated memory but it's ok we don't dereference it
    auto const * other_buffer = (next || !current.buffer) ? previous.buffer : nullptr; // only end crossfade with other if we started with him
    int const other_s = other_buffer? (int)other_buffer->values.size() : 0;
    int const s = current.buffer ? (int) current.buffer->values.size() : 0;
    for( int i=0; i<n_writes; i++ ) {
        auto val = 0.f;
        if(s) {
            if( current_next_sample_index == s ) {
                current_next_sample_index = 0;
            }
            A(current_next_sample_index < s);
            val = (1.f - xfade_ratio) * current.volume * current.buffer->values[current_next_sample_index];
            ++current_next_sample_index;
        }
        if(other_s) {
            A(other_next_sample_index >= 0);
            A(other_next_sample_index <= other_s);
            if(other_next_sample_index == other_s) {
                other_next_sample_index = 0;
            }
            A(other_next_sample_index <= other_s);
            val += xfade_ratio * previous.volume * previous.buffer->values[other_next_sample_index];
            ++other_next_sample_index;
        }
        xfade_ratio -= xfade_increment;
        write_value(val, outputBuffer);
    }
}


void outputData::Channel::write_xfade_left(SAMPLE * outputBuffer, float xfade_ratio, int const n_writes) {
    A(n_writes > 0);
//    LG(INFO, "<<<<< %d", n_writes);
    A(n_writes <= remaining_samples_count);
    A(xfade_ratio >= 0.f);
    A(xfade_ratio <= 1.f);
    auto * other = next ? &requests.front() : nullptr;
    int const other_s = other? static_cast<int>(other->buffer->values.size()) : 0;
    int const s = current.buffer ? (int) current.buffer->values.size() : 0;
    for( int i=0; i<n_writes; i++ ) {
        auto val = 0.f;
        if(s) {
            if( current_next_sample_index == s ) {
                current_next_sample_index = 0;
            }
            A(current_next_sample_index < s);
            val = xfade_ratio * current.volume * current.buffer->values[current_next_sample_index];
            ++current_next_sample_index;
        }
        if(other_s) {
            A(other_next_sample_index >= 0);
            A(other_next_sample_index <= other_s);
            if(other_next_sample_index == other_s) {
                other_next_sample_index = 0;
            }
            A(other_next_sample_index <= other_s);
            val += (1.f - xfade_ratio) * other->volume * other->buffer->values[other_next_sample_index];
            ++other_next_sample_index;
        }
        xfade_ratio -= xfade_increment;
        write_value(val, outputBuffer);
    }
}
