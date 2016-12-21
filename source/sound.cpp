
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
    c.playing.transition_volume_remaining = Channel::Playing::volume_transition_length;
    c.playing.channel_volume_increments = (vol - c.playing.channel_volume) / (float) Channel::Playing::volume_transition_length;
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
        delay.step(outputBuffer, framesPerBuffer);
    }
}

void outputData::Channel::step(SAMPLE * outputBuffer, int framesPerBuffer)
{
    playing.consume(requests);
    
    playing.write( outputBuffer, framesPerBuffer );
}

void outputData::Channel::Playing::consume(std::queue<Request> & requests) {
    if( remaining_samples_count > 0 ) {
        return;
    }
    if (requests.empty()) {
        return;
    }
    play(requests.front());
    requests.pop();
}

void outputData::Channel::Playing::play(Request & request) {
    if( request.duration_in_samples <= 0 ) {
        return;
    }
    sound = request.buffer;
    sound_volume = request.volume;
    remaining_samples_count = request.duration_in_samples;
    next_sample_index = 0;
}

constexpr float amplitude = 0.1f; // ok to have 10 chanels at max amplitude at the same time
void outputData::Channel::Playing::write(SAMPLE * outputBuffer, int framesPerBuffer) {
    auto s = -1;
    
    auto a = amplitude * sound_volume;
    
    for( int i=0; i<framesPerBuffer && remaining_samples_count > 0; i++ ) {
        if( s == -1 ) {
            s = (int) sound->values.size();
        }
        if( next_sample_index >= s ) {
            next_sample_index = 0;
        }
        
        if( transition_volume_remaining ) {
            transition_volume_remaining--;
            channel_volume += channel_volume_increments;
        }
        *outputBuffer += a * channel_volume * sound->values[next_sample_index];
        ++outputBuffer;
        ++next_sample_index;
        --remaining_samples_count;
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

