
namespace imajuscule {
    
    constexpr int ms_to_frames(float duration_ms) {
        return static_cast<int>( SAMPLE_RATE * 0.001f * duration_ms );
    }
    
    constexpr float frames_to_ms(float duration_frames) {
        return 1000.f * duration_frames / SAMPLE_RATE;
    }
    
    constexpr float int_period_to_freq(int period) {
        return SAMPLE_RATE / static_cast<float>(period);
    }
    
    constexpr int freq_to_int_period( float freq_hz ) {
        if(freq_hz <= 0.f) {
            return 1;
        }
        return static_cast<int>(SAMPLE_RATE / freq_hz);
    }

    template<typename T>
    constexpr T freq_to_angle_increment(T freq) {
        static_assert(std::is_floating_point<T>::value, "");
        return 2 * freq / SAMPLE_RATE;
    }

    template<typename T>
    constexpr T square(T ang) {
        static_assert(std::is_floating_point<T>::value, "");
        using Tr = NumTraits<T>;
        if( Tr::half() < ang && ang < Tr::one_and_half() ) {
            return -Tr::one();
        } else {
            return Tr::one();
        }
    }
    
    template<typename T>
    constexpr T pulse(T ang, T pulse_width) {
        static_assert(std::is_floating_point<T>::value, "");
        using Tr = NumTraits<T>;
        A(pulse_width >= 0);
        A(ang >= 0);
        A(ang <= 2);
        if( ang < pulse_width ) {
            return Tr::one();
        } else {
            return Tr::zero();
        }
    }
    
}
