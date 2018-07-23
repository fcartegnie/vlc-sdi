#ifndef AES3AUDIO_HPP
#define AES3AUDIO_HPP

#include <vlc_common.h>

#define MAX_AUDIO_PAIRS 8

namespace sdi_sout
{
    class AES3Frame
    {
        public:
            AES3Frame();
            virtual ~AES3Frame();
            virtual void *getPayload();

        private:
            size_t datasize;
            uint8_t data[192 * 4];
    };

    class AES3Audio
    {
        public:
            AES3Audio();

        private:
    };

    class AES3AudioPair
    {
        public:
            AES3AudioPair();

        private:
    };

    class SDIAudioMultiplex
    {
        public:
            SDIAudioMultiplex(unsigned pairs);
            ~SDIAudioMultiplex();

        private:
            unsigned count;
            AES3AudioPair *pairs[MAX_AUDIO_PAIRS];
    };
}

#endif // AES3AUDIO_HPP
