
using namespace imajuscule;

Request::Request( Sounds & sounds, Sound const & sound, float freq_hz, float volume, float duration_ms ) :
volume(volume)
{
    duration_in_samples = (int)( ((float)SAMPLE_RATE) * 0.001f * duration_ms );
    
    buffer = &sounds.get( { sound, freq_hz } );
    A(buffer);
    
    if( sound.zeroOnPeriodBoundaries() ) {
        const int period_size = (int)buffer->values.size();
        
        const int mod = duration_in_samples % period_size;
        if(mod) {
            duration_in_samples += period_size-mod;
        }
        A( 0 == duration_in_samples % period_size);
    }
}

outputData::DelayLine::DelayLine(int size, float attenuation): delay(size,0.f), it(0), end(size), attenuation(attenuation) {}

void outputData::DelayLine::step(SAMPLE *outputBuffer, int framesPerBuffer) {
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

uint8_t outputData::openChannel() {
    RAIILock l(used);
    
    auto index = available_ids.Take(channels);
    A(index != AUDIO_CHANNEL_NONE); // now channel vector is full!
    
    return index;
}

bool outputData::closeChannel(uint8_t channel_id) {
    RAIILock l(used);
    
    std::queue<Request> empty;
    editChannel(channel_id).requests.swap(empty);
    available_ids.Return(channel_id);
    return channels.empty();
}

void outputData::play( uint8_t channel_id, std::vector<Request> && v ) {
    RAIILock l(used);
    
    auto & c = editChannel(channel_id);
    for( auto & sound : v ) {
        c.requests.emplace( std::move(sound) );
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
        // deactivated on purpose : reactivate once we have cross fade
        // to see if it is ok, else need to low pass filtered
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
                    write_crossfading_from_zero( outputBuffer, xfade_ratio, xfade_written );
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
                write_crossfading_from_zero( outputBuffer, xfade_ratio, xfade_written );
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
    auto s = (int) current.buffer->values.size();
    auto const a = amplitude * current.volume;
    for( int i=0; i<n_writes; i++ ) {
        if( next_sample_index == s ) {
            next_sample_index = 0;
        }
        A(next_sample_index < s);
        
        if( transition_volume_remaining ) {
            transition_volume_remaining--;
            channel_volume += channel_volume_increments;
        }
        A(crossfading_from_zero_remaining() <= 0);
        *outputBuffer += a * channel_volume * current.buffer->values[next_sample_index];
        ++outputBuffer;
        ++next_sample_index;
    }
}

void outputData::Channel::write_crossfading_from_zero(SAMPLE * outputBuffer, float xfade_ratio, int const n_writes) {
    A(n_writes > 0);
//    LG(INFO, ">>>>> %d", n_writes);
    A(n_writes <= crossfading_from_zero_remaining());
    A(xfade_ratio >= 0.f);
    A(xfade_ratio <= 1.f);

    // note that next points probably to deallocated memory but it's ok we don't dereference it
    auto * other_buffer = (next || !current.buffer) ? previous.buffer : nullptr; // only end crossfade with other if we started with him
    auto other_s = other_buffer? (int)other_buffer->values.size() : 0;
    auto s = current.buffer ? (int) current.buffer->values.size() : 0;
    auto const a = amplitude;
    for( int i=0; i<n_writes; i++ ) {
        auto val = 0.f;
        if(s) {
            if( next_sample_index == s ) {
                next_sample_index = 0;
            }
            A(next_sample_index < s);
            val = (1.f - xfade_ratio) * current.volume * current.buffer->values[next_sample_index];
            ++next_sample_index;
        }
        if(other_s) {
            A(previous_next_sample_index >= 0);
            A(previous_next_sample_index <= other_s);
            if(previous_next_sample_index == other_s) {
                previous_next_sample_index = 0;
            }
            A(previous_next_sample_index <= other_s);
            val += xfade_ratio * previous.volume * previous.buffer->values[previous_next_sample_index];
            ++previous_next_sample_index;
        }
        
        if( transition_volume_remaining ) {
            transition_volume_remaining--;
            channel_volume += channel_volume_increments;
        }
        //printf("%.3f ", xfade_ratio); // to make sure we use the right ranges
        *outputBuffer += a * channel_volume * val;
        xfade_ratio -= xfade_increment;
        ++outputBuffer;
    }
}


void outputData::Channel::write_crossfading_to_zero(SAMPLE * outputBuffer, float xfade_ratio, int const n_writes) {
    A(n_writes > 0);
//    LG(INFO, "<<<<< %d", n_writes);
    A(n_writes <= remaining_samples_count);
    A(xfade_ratio >= 0.f);
    A(xfade_ratio <= 1.f);
    auto * other = next ? &requests.front() : nullptr;
    auto other_s = other? other->buffer->values.size() : 0;
    auto s = current.buffer ? (int) current.buffer->values.size() : 0;
    auto const a = amplitude;
    for( int i=0; i<n_writes; i++ ) {
        auto val = 0.f;
        if(s) {
            if( next_sample_index == s ) {
                next_sample_index = 0;
            }
            A(next_sample_index < s);
            val = xfade_ratio * current.volume * current.buffer->values[next_sample_index];
            ++next_sample_index;
        }
        if(other_s) {
            A(previous_next_sample_index >= 0);
            A(previous_next_sample_index <= other_s);
            if(previous_next_sample_index == other_s) {
                previous_next_sample_index = 0;
            }
            A(previous_next_sample_index <= other_s);
            val += (1.f - xfade_ratio) * other->volume * other->buffer->values[previous_next_sample_index];
            ++previous_next_sample_index;
        }

        if( transition_volume_remaining ) {
            transition_volume_remaining--;
            channel_volume += channel_volume_increments;
        }
        //printf("%.3f ", xfade_ratio); // to make sure we use the right ranges
        *outputBuffer += a * channel_volume * val;
        xfade_ratio -= xfade_increment;
        ++outputBuffer;
    }
}

static float triangle( float angle_radians ) {
    A(angle_radians >= 0.f);
    A(angle_radians <= 2.f * (float)M_PI);
    
    static const float inv_pi = 1.f / (float)M_PI;
    
    angle_radians *= inv_pi;
    if( angle_radians < 0.5f ) {        // 0 .. 0.5   ->  0 .. 1
        return 2.f * angle_radians;
    } else if( angle_radians < 1.5f ) { // 0.5 .. 1.5 ->  1 .. -1
        return 2.f - 2.f * angle_radians;
    } else {                            // 1.5 .. 2   ->  -1 .. 0
        A( angle_radians <= 2.f );
        return -4.f + 2.f * angle_radians;
    }
}
static float saw( float angle_radians ) {
    A(angle_radians >= 0.f);
    A(angle_radians <= 2.f * (float)M_PI);
    
    static const float inv_pi = 1.f / (float)M_PI;
    
    angle_radians *= inv_pi;
    
    // 0 .. 2 -> 1 .. -1
    return 1.f - angle_radians;
}
static float square( float angle_radians ) {
    A(angle_radians >= 0.f);
    A(angle_radians <= 2.f * (float)M_PI);
    
    static const float inv_pi = 1.f / (float)M_PI;
    
    angle_radians *= inv_pi;
    
    if( angle_radians <= 1.f ) { // 0 .. 1 ->  1
        return 1.f;
    } else {                    // 1 .. 2 ->  -1
        A(angle_radians <= 2.f);
        return -1.f;
    }
}

static float my_rand(float) {
    float between_zero_one = ((float)rand())/(float)(RAND_MAX);
    return (between_zero_one * 2.f) - 1.f;
}

template < typename F >
void soundBuffer::generate( int period, F f ) {
    
    // Let's compute the waveform. First sample is non zero, last sample is zero, so the mapping is:
    //
    //  sample(int) [0 .. period - 1]  ->  radian(float) [2*pi/period .. 2*pi]
    //
    float increment = 2.f * (float)M_PI / (float) period;
    
    for( int i=0; i<period;) {
        i++;
        values.emplace_back( f( increment * (float)i ) );
    }
    
    A( (int)values.size() == period );
}
soundBuffer::soundBuffer( soundId const & id ) {
    values.reserve( id.period_length );

    switch (id.sound.type) {
        case Sound::NOISE:
        {
            generate( id.period_length, my_rand );
            if( id.period_length < 20 ) {
                // fix for small number of random values
                {
                    // center around zero
                    
                    auto avg(0.f);
                    for( auto const & v : values ) {
                        avg += v;
                    }
                    avg /= (float)values.size();
                    for( auto & v : values ) {
                        v -= avg;
                    }
                }
                {
                    // maximize
                    
                    auto M(0.f);
                    for (auto const & v : values) {
                        M = std::max( M, std::abs( v ) );
                    }
                    if( M < 0.5 ) {
                        auto fact = 0.7f/M;
                        for( auto & v : values ) {
                            v *= fact;
                        }
                    }
                }
            }
            break;
        }

        case Sound::SINE:
            generate( id.period_length, sinf );
            break;
            
        case Sound::TRIANGLE:
            generate( id.period_length, triangle );
            break;
            
        case Sound::SAW:
            generate( id.period_length, saw );
            break;
            
        case Sound::SQUARE:
            generate( id.period_length, square );
            break;
            
        case Sound::SILENCE:
            generate( id.period_length, [](float){ return 0.f; } );
            break;
            
        case Sound::ONE:
            generate( id.period_length, [](float){ return 1.f; } );
            break;
            
        default:
            A(0);
            break;
    }
}

soundBuffer const & Sounds::get(soundId const & id ) {
    {
        auto it = sounds.find(id);
        if( it != sounds.end() ) {
            return it->second;
        }
    }
    auto it = sounds.emplace(id, id);
    A(it.second);
    return it.first->second;
}

