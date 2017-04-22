
namespace imajuscule {
    
    // this is a "one for all" type of class, initially designed to handle
    // wind which has infinite length notes (hence the way n_notes is modified)
    // and that is now used to play birds.
    
    template<typename OUT, typename INST>
    struct Instrument {
        
        Instrument(OUT & out) : out(out) {
            instrument.initializeSlow();
        }
        
        void startOneNote() { playOne(); }

        void stopOneNote() {}
        
        void setNotesCount(int n) { while(n > n_notes) { playOne(); } }
        
        void setRandom(bool b) { random = b; }
        void setSeed(int s) { seed = s; }
        void setProgram(int prog) { program = prog; }
        
        void setLinearVolume(float vol) {
            if(unlikely(vol < 0.f)) {
                LG(ERR, "negative volume");
                vol = 0.f;
            }
            else if(unlikely(vol > 50.f)) {
                LG(ERR, "too high volume");
                vol = 50.f;
            }
            volume = vol;
        }
        
        OUT& getOut() { return out; }

        auto const & getPrograms() const { return instrument.getPrograms(); }
    private:
        OUT & out;
        INST instrument;
        float volume = 1.f;
        int n_notes = 0;
        int seed = 1;
        bool random = false;
        float pan = 0.f;
        int program = 0;
        
        void playOne() {
            ++n_notes;
            audio::playOneThing(instrument,
                                out,
                                audio::Voicing{ program, volume, pan, random, seed});

        }
    };
    
}
