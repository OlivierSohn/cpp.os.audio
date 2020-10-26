
namespace imajuscule::audio {

// this is a "one for all" type of class, initially designed to handle
// wind which has infinite length notes (hence the way n_notes is modified)
// and that is now used to play birds.

template<typename OUT, typename INST>
struct Instrument {
  
  using Inst = INST;
  static constexpr auto n_mnc = Inst::n_channels;
  
  Instrument(OUT & out, int sample_rate)
  : instrument(std::make_unique<Inst>())
  , out(out)
  , sample_rate_(sample_rate) {
    instrument->initializeSlow();
    instrument->initialize(out);
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
  
  bool isPlaying() {
    if (auto c = getFirstChan()) {
      return c->hasRealtimeFunctions();
    }
    return false;
  }
  
  auto const & getPrograms() const { return instrument->getPrograms(); }
private:
  OUT & out;
  std::unique_ptr<INST> instrument;
  float volume = 1.f;
  int n_notes = 0;
  int16_t midiPitch = 50; // too low value to catch when it is not initialized
  int seed = 1;
  bool random = false;
  float pan = 0.f;
  int program = 0;
  Midi midi;
  int sample_rate_;
  
  void playOne() {
    ++n_notes;
    audio::playOneThing(sample_rate_,
                        midi,
                        *instrument,
                        out,
                        audio::Voicing{ program, midiPitch, volume, pan, random, seed});
    
  }

  typename OUT::ChannelsT::NoXFadeChans *
  getFirstChan() {
    if(auto m = out.getChannels().getChannelsNoXFade().maybe_front()) {
      return &get_value(m).first;
    }
    return {};
  }
  
};

}
