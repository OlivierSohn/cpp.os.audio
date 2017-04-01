
namespace imajuscule {
    template<typename OUT, typename INST>
    struct Instrument {
        
        Instrument(OUT & out) : out(out) {
            instrument.initializeSlow();
        }
        
        void startOneNote() { playOne(); }

        void stopOneNote() {}
        
        void setNotesCount(int n) { while(n > n_notes) { playOne(); } }
        
        void setRandom(bool b) { random = b; }
        
        void setLinearVolume(float vol) {
            if(unlikely(vol < 0.f)) {
                LG(ERR, "negative volume");
                vol = 0.f;
            }
            if(unlikely(vol > 50.f)) {
                LG(ERR, "too high volume");
                vol = 50.f;
            }
            volume = vol;
        }
        
        OUT& getOut() { return out; }
    private:
        OUT & out;
        INST instrument;
        float volume = 1.f;
        int n_notes = 0;
        bool random = false;
        
        void playOne() {
            ++n_notes;
            audio::playOneThing(instrument,
                                out,
                                audio::Bird{ 0, volume, 0.f, random });

        }
    };
    
}
