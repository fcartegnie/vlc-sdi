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

    class AFD : Ancillary
    {
        public:
            AFD(uint8_t afdcode, uint8_t ar);
            virtual ~AFD();
            virtual void FillBuffer(uint8_t *, size_t);

        private:
            uint8_t afdcode;
            uint8_t ar;
    };

}

#endif // ANCILLARY_HPP
