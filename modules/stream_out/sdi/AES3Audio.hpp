#ifndef AES3AUDIO_HPP
#define AES3AUDIO_HPP

#include <vlc_common.h>

namespace sdi_sout
{
    class SDIOutputStream;

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
            AES3Audio(unsigned channels, vlc_fourcc_t format);
            int addPair(unsigned num, SDIOutputStream *id);

        private:
            unsigned channels;
    };

#define MAX_AUDIO_PAIRS 8
    class SDIAudioMultiplex
    {
        public:
            SDIAudioMultiplex();
            ~SDIAudioMultiplex();

        private:
            struct
            {
                SDIOutputStream *id;
                uint8_t subframe_id_0;
                uint8_t subframe_id_1;
            } pairs[MAX_AUDIO_PAIRS];
    };
}

#endif // AES3AUDIO_HPP
