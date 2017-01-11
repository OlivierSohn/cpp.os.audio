
using namespace imajuscule;

Request::Request( Sounds & sounds, Sound const & sound, float freq_hz, float volume, float duration_ms ) :
volume(volume)
{
    duration_in_samples = (int)( ((float)SAMPLE_RATE) * 0.001f * duration_ms );
    
    buffer = &sounds.get( { sound, freq_hz } );
    A(buffer);
    
    if( sound.zeroOnPeriodBoundaries() ) {
        const int period_size = (int)buffer->values.size();
        if(period_size == 0) {
            return;
        }
        
        const int mod = duration_in_samples % period_size;
        if(mod) {
            duration_in_samples += period_size-mod;
        }
        A( 0 == duration_in_samples % period_size);
    }
}

