
namespace imajuscule {
    enum Note : unsigned char{
        NOTE_ERROR,
        Silence,
        Do,
        Dod,
        Re,
        Red,
        Mi,
        Fa,
        Fad,
        Sol,
        Sold,
        La,
        Lad,
        Si,
    };
    
    struct NoteSpec {
        Note note : 4;
        bool loud : 1;
        unsigned int duration : 7; // max. 128
    };
   
    static inline float compute_half_tone(float stretch) {
        return powf(2.f, stretch/12.f);
    }
    
    constexpr float freq_do = 261.64f;
    
    constexpr float to_freq(Note n, float half_tone) {
        auto diff = static_cast<int>(n) - static_cast<int>(Do);
        if(diff == 0) {
            return freq_do;
        }
        return freq_do * powf(half_tone, static_cast<float>(diff));
    }
    
    
    static inline float transpose_frequency(float freq, float half_tone, int n) {
        return freq * powf(half_tone, n);
    }

    inline Request to_request(NoteSpec s, float time_unit, float harmonic_factor, float half_tone, Sounds & sounds) {
        if(s.note == Silence) {
            return {
                sounds,
                Sound::SILENCE,
                Sound::ConstantSoundDummyFrequency,
                0.f,
                time_unit * (float)s.duration
            };
        }
        else {
            return {
                sounds,
                Sound::SINE,
                harmonic_factor * to_freq(s.note, half_tone),
                s.loud ? 2.f : 1.f,
                time_unit * (float)s.duration
            };
        }
    }
}
