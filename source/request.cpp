
using namespace imajuscule;

Request::Request( Sounds & sounds, Sound const sound, float freq_hz, float vol, float duration_ms ) :
volume(vol),
buffer(nullptr)
{
    A(duration_ms >= 0.f);
    duration_in_frames = ms_to_frames(duration_ms);

    // we silence some sounds instead of just not playing them, in order to keep
    // the rythm
    
    soundId Id;
    bool silence = false;
    if(freq_hz < 10.f) {
        LG(WARN, "silenced sound of inaudible (low) frequency '%.6f Hz'", freq_hz);
        silence = true;
    }
    else if(volume == 0.f) {
        silence = true;
    }
    else {
        Id = soundId{ sound, freq_hz };
        if(Id.period_length < sound.minimalPeriod()) {
            silence = true;
            LG(WARN, "silenced sound of inaudible (high) frequency '%.1f Hz'", freq_hz);
        }
    }
    if(silence) {
        buffer.reset(&sounds.get( {Sound::SILENCE} ));
        volume = 0.f;
    }
    else {
        auto & b = sounds.get( std::move(Id) );
        buffer.reset(&b);
        
        if( sound.zeroOnPeriodBoundaries() ) {
            const int period_size = (int)b.size();
            if(period_size == 0) {
                return;
            }
            
            if(0 == duration_in_frames) {
                duration_in_frames = period_size;
            }
            else {
                const int mod = duration_in_frames % period_size;
                if(mod) {
                    duration_in_frames += period_size-mod;
                }
            }
            
            A( 0 == duration_in_frames % period_size);
        }        
    }
}

