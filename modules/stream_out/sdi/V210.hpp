#ifndef V210_HPP
#define V210_HPP

#include <vlc_common.h>

namespace sdi
{

    class V210
    {
        public:
            static void Convert(const picture_t *, unsigned, void *);
    };

}

#endif // V210_HPP
