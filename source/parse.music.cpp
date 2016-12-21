namespace imajuscule {
    
    enum Note : unsigned char{
        NOTE_ERROR,
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
    
    namespace {
        constexpr float half_tone = 1.059463094359295f; // powf(2.f, 1.f/12.f);
        constexpr float freq_do = 200.f;
        
        constexpr float to_freq(Note n) {
            auto diff = static_cast<int>(n) - static_cast<int>(Do);
            if(diff == 0) {
                return freq_do;
            }
//            diff = 12;
            return freq_do * powf(half_tone, static_cast<float>(diff));
        }
        Note parse(Note note, std::string const & str, size_t pos) {
            A(note != NOTE_ERROR);
            if(str.size() <= pos) {
                return note;
            }
            if(str.size() > pos + 1) {
                LG(ERR, "Note error: '%s' has too many characters", str.c_str());
                return NOTE_ERROR;
            }
            switch(str[pos]) {
                case 'd':
                case 'D':
                case '#':
                case 's':
                    if(note == Si) {
                        return  Do;
                    }
                    return static_cast<Note>(note + 1);
                case 'b':
                case 'B':
                case 'f':
                    if(note == Do) {
                        return Si;
                    }
                    return static_cast<Note>(note - 1);
            }
            LG(ERR, "Note error: '%s' has an unrecognized end character", str.c_str());
            return NOTE_ERROR;
        }
        
        struct NoteClass {
            NoteClass(const char * str, Note n) : str(str), enum_(n) {}
            const char * str;
            Note enum_;
        };
        
        std::array<NoteClass,7> const notes = {{
            {"do", Do},
            {"re", Re},
            {"mi", Mi},
            {"fa", Fa},
            {"sol", Sol},
            {"la", La},
            {"si", Si},
        }};
        
        Note parseNote(std::string const & str) {
            for(auto const & note : notes) {
                if(auto pos = ibegins_with(str, note.str)) {
                    return parse( note.enum_, str, pos);
                }
            }
            LG(ERR, "Note error: '%s' is not recognized", str.c_str());
            return NOTE_ERROR;
        }
        
        struct NoteAlgo {
            NoteAlgo(float time_unit) : time_unit(time_unit) {}
            
            bool run(std::string const & score) {
                requests.reserve(score.size() / 3);
                
                size_t pos = 0;
                while(pos < score.size()) {
                    auto next = score.find_first_of(" .-", pos);
                    if(next == pos) {
                        switch(score[pos]) {
                            case ' ':
                                if(!make_note()) {
                                    return false;
                                }
                                break;
                            case '.':
                                if(current != NOTE_ERROR) {
                                    if(!make_note()) {
                                        return false;
                                    }
                                }
                                if(!make_silence() ) {
                                    return false;
                                }
                                break;
                            case '-':
                                if(current == NOTE_ERROR) {
                                    LG(ERR, "found no note before '-'");
                                    return false;
                                }
                                ++duration;
                                break;
                            default:
                                A(0);
                                LG(ERR, "logic error");
                                return false;
                        }
                        ++pos;
                    }
                    else {
                        if(current != NOTE_ERROR) {
                            if(!make_note()) {
                                return false;
                            }
                        }
                        auto str = score.substr(pos, next-pos);
                        pos = next;
                        if(str.empty()) {
                            return false;
                        }
                        accent = isupper(str[0]);
                        current = parseNote(std::move(str));
                        if(current == NOTE_ERROR) {
                            return false;
                        }
                        // do not include the note yet, we need to know how long it will last
                    }
                }
                
                if(current != NOTE_ERROR) {
                    if(!make_note()) {
                        return false;
                    }
                }

                return true;
            }
            
            std::vector<Request> requests;
        private:
            Note current = NOTE_ERROR, previous = NOTE_ERROR;
            int duration = 1;
            bool accent = false;
            float time_unit;
            
            bool make_note() {
                if(current == NOTE_ERROR) {
                    LG(ERR, "note is error");
                    return false;
                }
                auto a = Audio::getInstance();
                if(!a) {
                    LG(ERR, "no audio");
                    return false;
                }
                
                requests.emplace_back(a->out().editSounds(),
                                      Sound::SINE,
                                      to_freq(current),
                                      accent? 2.f : 1.f,
                                      time_unit * (float)duration);
                
                accent = false;
                duration = 1;
                current = NOTE_ERROR;
                return true;
            }
            
            bool make_silence() {
                A(duration == 1);
                A(current == NOTE_ERROR);
                auto a = Audio::getInstance();
                if(!a) {
                    LG(ERR, "no audio");
                    return false;
                }
                
                requests.emplace_back(a->out().editSounds(),
                                      Sound::SILENCE,
                                      0.f,
                                      0.f,
                                      time_unit * (float)duration);
                return true;
            }
            
        };
        
        void normalize(std::string & score) {
            for(auto &c : score) {
                // allow user to write ré or re or RÉ
                if(c == static_cast<char>(130) /*'é'*/ || c == static_cast<char>(144) /*'É'*/) {
                    c = 'e';
                }
            }
        }
    }
    
    std::vector<Request> parseMusic(std::string score, float time_unit) {
        normalize(score);
        NoteAlgo a(time_unit);
        if( !a.run(std::move(score))) {
            LG(ERR, "not all music could be parsed");
        }
        return std::move(a.requests);
    }
    
}
