
using namespace imajuscule;

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
                    auto xfade_ratio = (float)(xfade_written-1) / (float)(2 * size_half_xfade);
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
                auto xfade_ratio = (float)(xfade_written-1) / (float)(2 * size_half_xfade);
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
    auto xfade_increment = get_xfade_increment();
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
    auto xfade_increment = get_xfade_increment();
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
