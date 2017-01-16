namespace imajuscule {

    constexpr auto cache_line_n_bytes = 64;
    
    struct AudioElementBase {
        // AudioComponent<float> has a buffer of size 1 cache line
        // AudioComponent<double> has a buffer of size 2 cache lines
        // each of them have 16 frames worth of data in their buffer
        static constexpr auto n_frames_per_buffer = cache_line_n_bytes / 4;
        static constexpr auto buffer_alignment = cache_line_n_bytes;
    };
    
    template<typename T>
    struct AudioElement : public AudioElementBase {
        // todo review the strategy : an Oscillator has an addition 4 floats to store, so 16 - 4 floats will be wasted on the second cache line
        // if an AudioElement is stored contiguously after...
        // maybe it's best to keep a pointer (or index if we have a buffer pool) to the buffer, to be sure there will be no waste
        using buffer_placeholder_t = std::aligned_storage_t<n_frames_per_buffer * sizeof(T), buffer_alignment>;
        static_assert(alignof(buffer_placeholder_t) == buffer_alignment,"");
        
        // [AudioElement] beginning of the 1st cache line
        union {
            buffer_placeholder_t placeholder; // used to constrain alignment
            T buffer[n_frames_per_buffer];
        };

        // [AudioElement<float>] beginning of the 2nd cache line
        // [AudioElement<double>] beginning of the 3rd cache line
        bool empty : 1;
        bool clock_ : 1;
        
        using FPT = T;
        using Tr = NumTraits<T>;
        
        AudioElement() : empty(true) {
            A(0 == reinterpret_cast<unsigned long>(buffer) % buffer_alignment);
        }
    };
    
    /*
     * Phase controlled oscillator 
     *
     * First introduced to measure the error of Oscillator
     */
    template<typename T>
    struct PCOscillator : AudioElement<T> {
        using typename AudioElement<T>::Tr;

        PCOscillator() = default;
        PCOscillator(T angle_increments) { setAngleIncrements(angle_increments); }
        
        void setAngleIncrements(T v) {
            angle_increments = v;
        }
        
        void step() {
            angle_ += angle_increments;
            if(angle_ > Tr::two()) {
                angle_ -= Tr::two();
            }
            else if(angle_ < Tr::zero()) {
                angle_ += Tr::two();
            }
        }
        
        T real() const { return std::cos(M_PI*angle_); }
        T imag() const { return std::sin(M_PI*angle_); }
        
        T angle() const { return M_PI*angle_; }
        
    private:
        
        T angle_ = Tr::zero();
        T angle_increments;
    };
    
    template<typename T>
    constexpr T angle_increment_from_freq(T freq) {
        return 2 * freq / SAMPLE_RATE;
    }

    enum eNormalizePolicy {FAST_NORMALIZE, ACCURATE_NORMALIZE};
    
    template<typename T, int NormPolicy = FAST_NORMALIZE>
    struct Oscillator : AudioElement<T> {
        using typename AudioElement<T>::Tr;

        using AudioElement<T>::buffer;
        using AudioElement<T>::clock_;
        using AudioElement<T>::empty;

        constexpr Oscillator(T angle_increments) { setAngleIncrements(angle_increments); }
        constexpr Oscillator() : mult(Tr::one(), Tr::zero()) {}
        
        constexpr void setAngleIncrements(T f) {
            mult = polar(static_cast<T>(M_PI)*f);
        }
        
        void step() {
            cur *= mult;
            if(NormPolicy == FAST_NORMALIZE) {
                approx_normalize(); // to fix iterative error accumulation... if it is costly it could be done less frequently
            }
            else {
                normalize();
            }
        }
        
        void compute(bool const sync_clock) {
            if(empty) {
                empty = false;
            }
            else if(sync_clock == clock_) {
                return;
            }
            clock_ = sync_clock;
            for(auto & e : buffer) {
                step();
                e = real();
            }
        }
        
        T real() const { return cur.real(); }
        T imag() const { return cur.imag(); }
        
        T angle() const { return arg(cur); }

    private:
        complex<T> cur = {Tr::one(), Tr::zero()};
        complex<T> mult;
        
        constexpr void approx_normalize() {
            // http://dsp.stackexchange.com/questions/971/how-to-create-a-sine-wave-generator-that-can-smoothly-transition-between-frequen
            
            cur *= Tr::half() * (Tr::three() - norm(cur));
        }
        
        constexpr void normalize() {
            cur /= abs(cur);
        }
    };
 
    template<typename T>
    struct Ramp : AudioElement<T> {
        
        static_assert(std::is_same<T,float>::value, "non float interpolation is not supported");
        
        using typename AudioElement<T>::Tr;

        using AudioElement<T>::buffer;
        using AudioElement<T>::clock_;
        using AudioElement<T>::empty;

        Ramp(T from_,
             T to_,
             T duration_in_samples,
             itp::interpolation i = itp::LINEAR) :
        from(angle_increment_from_freq(from_)),
        to(angle_increment_from_freq(to_)),
        duration_in_samples(duration_in_samples),
        cur_sample(Tr::zero())
        {
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
            osc.step(); // we step osc because we own it
            cur_sample += C * f ;
        }
        
        void compute(bool const sync_clock) {
            if(empty) {
                empty = false;
            }
            else if(sync_clock == clock_) {
                return;
            }
            clock_ = sync_clock;
            for(auto & e : buffer) {
                step();
                e = real();
            }
        }

        T real() const { return osc.real(); }
        T imag() const { return osc.imag(); }
        
    private:
        Oscillator<T> osc;
        NormalizedInterpolation<T> interp;
        T from, to, cur_sample, C;
        const T duration_in_samples;
        
        T currentFreq() {
            if(cur_sample > duration_in_samples) {
                cur_sample = Tr::zero();
                std::swap(from, to);
            }
            // call get_unfiltered_value instead of get_value because we ensure:
            A(cur_sample <= duration_in_samples);
            return interp.get_unfiltered_value(cur_sample, duration_in_samples, from, to);
        }
    };
    
    template<class W1, class W2, typename T = typename W1::FPT>
    struct RingModulation : public AudioElement<T> {
        using typename AudioElement<T>::Tr;

        static_assert(std::is_same<T,typename W2::FPT>::value, "");

        RingModulation(W1&w1, W2&w2)
        : w1(w1), w2(w2)
        {}
        
        void step() {
            // we don't own w1 and w2 so we should probably not step them
            // w1.step();
            // w2.step();
        }
        
        T real() const { return w1.real() * w2.real(); }
        T imag() const { return w1.imag() * w2.imag(); }

    private:
        W1 & w1;
        W2 & w2;
    };
}
