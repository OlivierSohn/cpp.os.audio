
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
   
    constexpr float half_tone = 1.059463094359295f; // powf(2.f, 1.f/12.f);
    constexpr float freq_do = 200.f;
    
    constexpr float to_freq(Note n) {
        auto diff = static_cast<int>(n) - static_cast<int>(Do);
        if(diff == 0) {
            return freq_do;
        }
        return freq_do * powf(half_tone, static_cast<float>(diff));
    }

    inline Request to_request(NoteSpec s, float time_unit, float harmonic_factor, Sounds & sounds) {
        if(s.note == Silence) {
            return {
                sounds,
                Sound::SILENCE,
                0.f,
                0.f,
                time_unit * (float)s.duration
            };
        }
        else {
            return {
                sounds,
                Sound::SINE,
                harmonic_factor * to_freq(s.note),
                s.loud ? 2.f : 1.f,
                time_unit * (float)s.duration
            };
        }
    }
}
