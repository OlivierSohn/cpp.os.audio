
namespace imajuscule {
    
    // this is a "one for all" type of class, initially designed to handle
    // wind which has infinite length notes (hence the way n_notes is modified)
    // and that is now used to play birds.
    
    template<typename OUT, typename INST>
    struct Instrument {
        
        using Inst = INST;
      
      static constexpr auto n_mnc = Inst::n_channels;
      using mnc_buffer = typename Inst::MonoNoteChannel::buffer_t;

        Instrument(OUT & out) :
        instrument(create(buffers))
      , out(out) {
            instrument->initializeSlow();
            instrument->initialize(out.getChannels());
        }
        
        ~Instrument() {
            instrument->finalize();
        }
        
        void startOneNote() { playOne(); }

        void stopOneNote() {}
        
        void setNotesCount(int n) { while(n > n_notes) { playOne(); } }
        
        void setRandom(bool b) { random = b; }
        void setSeed(int s) { seed = s; }
        void setProgram(int prog) { program = prog; }
        void setPan(float p) { pan = p; }
        void setPitch(int16_t p) {
            midiPitch = p;
        }
        
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

        bool isPlaying() const {
            return out.getChannels().hasOrchestratorsOrComputes();
        }
        
        auto const & getPrograms() const { return instrument->getPrograms(); }
    private:
        OUT & out;
        std::array<mnc_buffer,n_mnc> buffers;
      std::unique_ptr<INST> instrument;
        float volume = 1.f;
        int n_notes = 0;
        int16_t midiPitch = 50; // too low value to catch when it is not initialized
        int seed = 1;
        bool random = false;
        float pan = 0.f;
        int program = 0;
        
        void playOne() {
            ++n_notes;
            audio::playOneThing(*instrument,
                                out,
                                out.getChannels(),
                                audio::Voicing{ program, midiPitch, volume, pan, random, seed});

        }
      
      
      template <class T, size_t N, size_t... Is>
      static auto create_int(std::array<T, N> & arr,
                             std::index_sequence<Is...>) {
        return new Inst(arr[Is]...);
      }
      
      template <class T, size_t N>
      static auto create(std::array<T, N> & arr) {
        return create_int(arr, std::make_index_sequence<N>{});
      }
      
    };
    
}
