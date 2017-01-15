namespace imajuscule {
    
    namespace {
        
        Note parse(Note note, std::string const & str, size_t pos) {
            A(note != NOTE_ERROR);
            A(note != Silence);
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
                    return safe_cast<Note>(note + 1);
                case 'b':
                case 'B':
                case 'f':
                    if(note == Do) {
                        return Si;
                    }
                    return safe_cast<Note>(note - 1);
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
            
            bool run(std::string const & score) {
                resetCurrent();

                notespecs.reserve(score.size());
                
                size_t pos = 0;
                while(pos < score.size()) {
                    auto next = score.find_first_of(" .-", pos);
                    if(next == pos) {
                        switch(score[pos]) {
                            case ' ':
                                break;
                            case '.':
                                if(current.note != Silence) {
                                    if(!make_note()) {
                                        return false;
                                    }
                                }
                                else {
                                    current.note = Silence;
                                }
                                ++ current.duration;
                                break;
                            case '-':
                                ++ current.duration;
                                break;
                            default:
                                A(0);
                                LG(ERR, "logic error");
                                return false;
                        }
                        ++pos;
                    }
                    else {
                        if(!make_pending()) {
                            return false;
                        }
                        auto str = score.substr(pos, next-pos);
                        pos = next;
                        if(str.empty()) {
                            return false;
                        }
                        A(current.duration == 0);
                        current.loud = isupper(str[0]);
                        current.note = parseNote(std::move(str));
                        if(current.note == NOTE_ERROR) {
                            return false;
                        }
                        current.duration = 1;
                        // do not include the note yet, we need to know how long it will last
                    }
                }
                
                return make_pending();
            }
            
            StackVector<NoteSpec> notespecs;
        private:
            NoteSpec current;
            
            bool make_pending() {
                if(current.duration == 0) {
                    return true;
                }
                if(current.note == Silence) {
                    return make_silence();
                }
                return make_note();
            }
            
            bool make_note() {
                if(current.note == NOTE_ERROR) {
                    LG(ERR, "note is error");
                    return false;
                }
                if(current.note == Silence) {
                    LG(ERR, "note is silence");
                    return false;
                }
                notespecs.emplace_back(current);
                resetCurrent();
                return true;
            }
            
            bool make_silence() {
                A(current.note == Silence);
                notespecs.emplace_back(current);
                resetCurrent();
                return true;
            }
            
            void resetCurrent()
            {
                current.note = Silence;
                current.duration = 0;
                current.loud = false;
            }
        };
        
        void normalize(std::string & score) {
            for(auto &c : score) {
                // allow user to write ré or re or RÉ
                if(c == safe_cast<char>(130) /*'é'*/ || c == safe_cast<char>(144) /*'É'*/) {
                    c = 'e';
                }
            }
        }
    }
    
    StackVector<NoteSpec> parseMusic(std::string score) {
        normalize(score);
        NoteAlgo a;
        if( !a.run(std::move(score))) {
            LG(ERR, "not all music could be parsed");
        }
        return std::move(a.notespecs);
    }
    
}
