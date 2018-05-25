
namespace imajuscule {
    AudioLockPolicyImpl<AudioOutPolicy::Master> & audioLock() {
        static AudioLockPolicyImpl<AudioOutPolicy::Master> l;
        return l;
    }
}
