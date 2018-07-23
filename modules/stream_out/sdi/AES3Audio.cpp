#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#include "AES3Audio.hpp"

using namespace sdi_sout;

AES3Audio::AES3Audio(unsigned channels_, vlc_fourcc_t)
{
    channels = channels_;
}
