namespace imajuscule {
    namespace audioelement {
        
        static constexpr auto n_frames_per_buffer = 16;

        struct AudioElementBase  {
            AudioElementBase() = default;
            
            // no copy or move because the lambda returned by fCompute() captures this
            AudioElementBase(const AudioElementBase &) = delete;
            AudioElementBase & operator=(const AudioElementBase&) = delete;
            AudioElementBase(AudioElementBase &&) = delete;
            AudioElementBase& operator = (AudioElementBase &&) = delete;
            
            // AudioComponent<float> has a buffer of size 1 cache line
            // AudioComponent<double> has a buffer of size 2 cache lines
            // each of them have 16 frames worth of data in their buffer
            static constexpr auto buffer_alignment = cache_line_n_bytes; // 64 or 32
            
            static constexpr auto index_state = 0;
        };
        
        template<typename T>
        auto & state(T * buffer) { return buffer[AudioElementBase::index_state]; }
        
        // lifecycle :
        // upon creation, state is inactive()
        // when in a queue state is queued()
        // when processed state is a float
        // when done being played state is inactive()
        template<typename T>
        struct AudioElement : public AudioElementBase {
            using buffer_placeholder_t = std::aligned_storage_t<n_frames_per_buffer * sizeof(T), buffer_alignment>;
            static_assert(alignof(buffer_placeholder_t) == buffer_alignment,"");
            
            // state values must be distinct from every possible valid value
            static constexpr auto queued() { return -std::numeric_limits<T>::infinity(); } // in *** at most *** one queue
            static constexpr auto inactive() { return std::numeric_limits<T>::infinity(); }// not active in any queue
            
            
            ////// [AudioElement] beginning of the 1st cache line
            
            union {
                buffer_placeholder_t for_alignment;
                T buffer[n_frames_per_buffer];
            };
            
            ////// [AudioElement<float>] beginning of the 2nd cache line
            ////// [AudioElement<double>] beginning of the 3rd cache line
            
            bool clock_ : 1; // could be removed since we don't have any AudioElement
            // depend on another
            
            using FPT = T;
            using Tr = NumTraits<T>;
            
            AudioElement() {
                A(0 == reinterpret_cast<unsigned long>(buffer) % buffer_alignment);
                state(buffer) = inactive();
            }
            
            auto getState() const { return state(buffer); }
            constexpr bool isInactive() const { return getState() == inactive(); }
            constexpr bool isActive() const { return getState() != inactive(); }
        };
        
        template<typename ALGO, typename T = decltype(ALGO().imag())>
        struct FinalAudioElement : public AudioElement<T>{
            using FPT = T;
            template <class... Args>
            FinalAudioElement(Args&&... args) : algo(std::forward<Args>(args)...) {}
            
            ALGO algo;
        };
        
        template<typename T>
        struct Phased {
            using Tr = NumTraits<T>;
            
            Phased() = default;
            Phased(T angle_increments) { setAngleIncrements(angle_increments); }
            
            void setAngle( T angle ) { angle_ = angle; }
            T angle() const { return angle_; }
            
            void setAngleIncrements(T v) {
                A(std::abs(v) < Tr::two()); // else need to modulo it
                angle_increments = v;
            }
            T angleIncrements() const { return angle_increments; }
            
            void step() {
                angle_ += angle_increments;
                if(angle_ > Tr::two()) {
                    angle_ -= Tr::two();
                }
                else if(angle_ < Tr::zero()) {
                    angle_ += Tr::two();
                }
            }
            
            protected:
            T angle_ = Tr::zero();
            T angle_increments;
        };
        
        /*
         * Phase controlled oscillator
         */
        template<typename T>
        struct PCOscillatorAlgo : public Phased<T> {
            using Phased<T>::angle_;
            
            PCOscillatorAlgo() = default;
            PCOscillatorAlgo(T angle_increments) : Phased<T>(angle_increments) {}
            
            T real() const { return std::cos(M_PI*angle_); }
            T imag() const { return std::sin(M_PI*angle_); }
        };
        
        template<typename T>
        using PCOscillator = FinalAudioElement<PCOscillatorAlgo<T>>;
        
        template<typename T>
        struct SquareAlgo : public Phased<T> {
            using Phased<T>::angle_;
            
            SquareAlgo() = default;
            SquareAlgo(T angle_increments) : Phased<T>(angle_increments) {}
            
            T imag() const { return square(angle_); }
        };

        template<typename T>
        using Square = FinalAudioElement<SquareAlgo<T>>;
        
        /*
         * first pulse happends at angle = 0
         */
        template<typename T>
        struct PulseTrainAlgo : public Phased<T> {
            using Tr = NumTraits<T>;
            using Phased<T>::angle_;
            
            PulseTrainAlgo() = default;
            PulseTrainAlgo(T angle_increments, T pulse_width) :
            Phased<T>(angle_increments),
            pulse_width(pulse_width) {
                A(pulse_width >= angle_increments); // else it's always 0
            }
            
            void set(T angle_increments, T pulse_width_) {
                A(pulse_width_ >= angle_increments); // else it's always 0
                this->setAngleIncrements(angle_increments);
                pulse_width = pulse_width_;
            }
            
            T imag() const { return pulse(angle_, pulse_width); }
        private:
            T pulse_width{};
        };
        
        template<typename T>
        using PulseTrain = FinalAudioElement<PulseTrainAlgo<T>>;

        template<typename AEAlgo, typename T = decltype(AEAlgo().imag())>
        struct LowPassAlgo {
        private:
            AEAlgo audio_element;
            Filter<T, 1, FilterType::LOW_PASS> low_pass;
        public:
            void step() {
                audio_element.step();
                auto val = audio_element.imag();
                low_pass.feed(&val);
            }
            
            T imag() const {
                return *low_pass.filtered();
            }
            
            auto & get_element() { return audio_element; }
            auto & filter() { return low_pass; }
        };
        
        
        template<typename AEAlgo>
        using LowPass = FinalAudioElement<LowPassAlgo<AEAlgo>>;
        
        enum class eNormalizePolicy {
            FAST,
            ACCURATE
        };

        template<typename T, eNormalizePolicy NormPolicy = eNormalizePolicy::FAST>
        struct OscillatorAlgo {
            using Tr = NumTraits<T>;
            
            constexpr OscillatorAlgo(T angle_increments) { setAngleIncrements(angle_increments); }
            constexpr OscillatorAlgo() : mult(Tr::one(), Tr::zero()) {}
            
            void setAngle(T f) {
                cur = polar(static_cast<T>(M_PI)*f);
            }
            void setAngleIncrements(T f) {
                mult = polar(static_cast<T>(M_PI)*f);
            }
            
            void step() {
                cur *= mult;
                if(NormPolicy == eNormalizePolicy::FAST) {
                    approx_normalize(); // to fix iterative error accumulation... if it is costly it could be done less frequently
                }
                else {
                    normalize();
                }
            }
            
            T real() const { return cur.real(); }
            T imag() const { return cur.imag(); }
            
            T angle() const { return arg(cur)/M_PI; }
            T angleIncrements() const { return arg(mult)/M_PI; }
            
            private:
            complex<T> cur = {Tr::one(), Tr::zero()};
            complex<T> mult;
            
            void approx_normalize() {
                // http://dsp.stackexchange.com/questions/971/how-to-create-a-sine-wave-generator-that-can-smoothly-transition-between-frequen
                
                cur *= Tr::half() * (Tr::three() - norm(cur));
            }
            
            void normalize() {
                cur /= abs(cur);
            }
        };
        
        template<typename T, eNormalizePolicy NormPolicy = eNormalizePolicy::FAST>
        using Oscillator = FinalAudioElement<OscillatorAlgo<T, NormPolicy>>;
        
        template<typename T>
        struct FreqRampAlgo {
            
            static_assert(std::is_same<T,float>::value, "non-float interpolation is not supported");
            
            using Tr = NumTraits<T>;
            
            FreqRampAlgo() : cur_sample(Tr::zero()), from{}, to{}, duration_in_samples{}
            {}
            
            void set(T from_,
                     T to_,
                     T duration_in_samples_,
                     T start_sample = Tr::zero(),
                     itp::interpolation i = itp::LINEAR)
            {
                cur_sample = start_sample;
                
                from = freq_to_angle_increment(from_);
                to = freq_to_angle_increment(to_);
                duration_in_samples = duration_in_samples_;
                
                // we want to achieve the same effect as PROPORTIONAL_VALUE_DERIVATIVE
                // but without paying the cost of one 'expf' call per audio frame :
                // to achieve the same effect we add to cur_sample a value proportionnal to
                // the current frequency. the factor of proportionnality is adjusted to match
                // the wanted duration_in_samples
                A(from > 0);
                A(to > 0);
                // else C computation cannot be done
                
                A(duration_in_samples > 0);
                interp.setInterpolation(i);
                
                C = -std::log(from/to) / (to-from);
            }
            
            void step() {
                auto f = currentFreq();
                osc.setAngleIncrements(f);
                osc.step(); // we must step osc because we own it
                cur_sample += C * f ;
            }
            
            T angleIncrements() const { return osc.angleIncrements(); }
            
            T real() const { return osc.real(); }
            T imag() const { return osc.imag(); }
            
        private:
            OscillatorAlgo<T> osc;
            NormalizedInterpolation<T> interp;
            T from, to, cur_sample, C;
            T duration_in_samples;
            
            T currentFreq() {
                if(cur_sample > duration_in_samples) {
                    cur_sample = Tr::zero();
                    std::swap(from, to);
                }
                // we call get_unfiltered_value instead of get_value because we ensure:
                A(cur_sample <= duration_in_samples);
                return interp.get_unfiltered_value(cur_sample, duration_in_samples, from, to);
            }
        };
        
        template<typename T>
        using FreqRamp = FinalAudioElement<FreqRampAlgo<T>>;
        
        template<typename A1, typename A2>
        struct RingModulationAlgo {
            using T = decltype(A1().imag());
            using Tr = NumTraits<T>;
            
            RingModulationAlgo() = default;
            
            void set(T angle_increments1, T angle_increments2, bool reset = true) {
                osc1.setAngleIncrements(angle_increments1);
                if(reset) {
                    osc1.setAngle(0.f);
                }
                osc2.setAngleIncrements(angle_increments2);
                if(reset) {
                    osc1.setAngle(0.f);
                }
            }
            
            void step() {
                osc1.step();
                osc2.step();
            }
            
            T real() const { return osc1.real() * osc2.real(); }
            T imag() const { return osc1.imag() * osc2.imag(); }
            
            auto & get_element_1() { return osc1; }
            auto & get_element_2() { return osc2; }
        private:
            A1 osc1;
            A2 osc2;
        };
        
        template<typename A1, typename A2>
        using RingModulation = FinalAudioElement<RingModulationAlgo<A1,A2>>;
        
        /*
         * returns false when the buffer is done being used
         */
        using ComputeFunc = std::function<bool(bool)>;
        
        template<typename T>
        struct FCompute {
            template<typename U=T>
            static auto get(U & ae)
            -> std::enable_if_t<
            IsDerivedFrom<U, AudioElementBase>::Is,
            ComputeFunc
            >
            {
                return [&ae](bool sync_clock) {
                    using AE = AudioElement<typename T::FPT>;
                    auto & e = safe_cast<AE&>(ae);
                    
                    if(e.getState() == AE::inactive()) {
                        // Issue : if the buffer just got marked inactive,
                        // but no new AudioElementCompute happends
                        // and from the main thread someone acquires this and queues it,
                        // it will have 2 lambdas because the first lambda will never have seen the inactive state.
                        // However the issue is not major, as the 2 lambdas have a chance to be removed
                        // the next time
                        return false;
                    }
                    if(e.getState() != AE::queued() && (sync_clock == e.clock_)) {
                        return true;
                    }
                    e.clock_ = sync_clock;
                    for(auto & v : e.buffer) {
                        ae.algo.step();
                        v = ae.algo.imag();
                    }
                    A(e.getState() != AE::queued());
                    A(e.getState() != AE::inactive());
                    return true;
                };
            }
            
            template<typename U=T>
            static auto get(U & e)
            -> std::enable_if_t<
            !IsDerivedFrom<U, AudioElementBase>::Is,
            ComputeFunc
            >
            {
                return {};
            }
        };
        
        template<typename T>
        ComputeFunc fCompute(T & e) {
            return FCompute<T>::get(e);
        }
        
    } // NS audioelement
} // NS imajuscule
