#ifndef ANCILLARY_HPP
#define ANCILLARY_HPP

#include <vlc_common.h>

namespace sdi
{

    class Ancillary
    {
        public:
            virtual void FillBuffer(uint8_t *, size_t) = 0;
    };

    class AFD : public Ancillary
    {
        public:
            AFD(uint8_t afdcode, uint8_t ar);
            virtual ~AFD();
            virtual void FillBuffer(uint8_t *, size_t);

        private:
            uint8_t afdcode;
            uint8_t ar;
    };

    class Captions : public Ancillary
    {
        public:
            Captions(const uint8_t *, size_t, unsigned, unsigned);
            virtual ~Captions();
            virtual void FillBuffer(uint8_t *, size_t);

        private:
            const uint8_t *p_data;
            size_t i_data;
            unsigned rate;
    };
}

#endif // ANCILLARY_HPP
