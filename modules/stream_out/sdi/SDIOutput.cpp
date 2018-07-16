#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#include "SDIOutput.hpp"
#include "SDIOutputStream.hpp"
#include <vlc_sout.h>

using namespace sdi_sout;

SDIOutput::SDIOutput(sout_stream_t *p_stream_)
{
    p_stream = p_stream_;
    p_stream->pf_add     = SoutCallback_Add;
    p_stream->pf_del     = SoutCallback_Del;
    p_stream->pf_send    = SoutCallback_Send;
    p_stream->pf_flush   = SoutCallback_Flush;
    p_stream->pf_control = SoutCallback_Control;

}

SDIOutput::~SDIOutput()
{

}

AbstractStream *SDIOutput::Add(const es_format_t *fmt)
{
    if( fmt->i_cat != VIDEO_ES )
        return NULL;

     AbstractStream *s = new VideoDecodedStream(VLC_OBJECT(p_stream));
     if(!s->init(fmt))
     {
         delete s;
         return NULL;
     }
     return s;
}

void SDIOutput::Del(AbstractStream *s)
{
    delete s;
}

int SDIOutput::Control(int, va_list)
{
    return VLC_EGENERIC;
}

void *SDIOutput::SoutCallback_Add(sout_stream_t *p_stream, const es_format_t *fmt)
{
    SDIOutput *me = reinterpret_cast<SDIOutput *>(p_stream->p_sys);
    return me->Add(fmt);
}

void SDIOutput::SoutCallback_Del(sout_stream_t *p_stream, void *id)
{
    SDIOutput *me = reinterpret_cast<SDIOutput *>(p_stream->p_sys);
    me->Del(reinterpret_cast<AbstractStream *>(id));
}

int SDIOutput::SoutCallback_Send(sout_stream_t *, void *id, block_t *p_block)
{
    return reinterpret_cast<AbstractStream *>(id)->Send(p_block);
}

int SDIOutput::SoutCallback_Control(sout_stream_t *p_stream, int query, va_list args)
{
    SDIOutput *me = reinterpret_cast<SDIOutput *>(p_stream->p_sys);
    return me->Control(query, args);
}

void SDIOutput::SoutCallback_Flush(sout_stream_t *, void *id)
{
    reinterpret_cast<AbstractStream *>(id)->Flush();
}
