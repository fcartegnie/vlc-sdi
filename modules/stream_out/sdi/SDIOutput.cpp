#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#include "SDIOutput.hpp"
#include "SDIStream.hpp"
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
    p_stream->pace_nocontrol = true;
}

SDIOutput::~SDIOutput()
{
    videoBuffer.FlushQueued();
    audioBuffer.FlushQueued();
    captionsBuffer.FlushQueued();
}

AbstractStream *SDIOutput::Add(const es_format_t *fmt)
{
    AbstractStream *s;
    if(fmt->i_cat == VIDEO_ES)
        s = new VideoDecodedStream(VLC_OBJECT(p_stream), &videoBuffer);
    else if(fmt->i_cat == AUDIO_ES)
        s = new AudioDecodedStream(VLC_OBJECT(p_stream), &audioBuffer);
    else if(fmt->i_cat == SPU_ES && fmt->i_codec == VLC_CODEC_CEA608)
        s = new CaptionsStream(VLC_OBJECT(p_stream), &captionsBuffer);
    else
        s = NULL;

     if(s && !s->init(fmt))
     {
         delete s;
         return NULL;
     }
     return s;
}

int SDIOutput::Send(AbstractStream *id, block_t *p)
{
    int ret = id->Send(p);
    Process();
    return ret;
}

void SDIOutput::Del(AbstractStream *s)
{
    s->Drain();
    Process();
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

int SDIOutput::SoutCallback_Send(sout_stream_t *p_stream, void *id, block_t *p_block)
{
    SDIOutput *me = reinterpret_cast<SDIOutput *>(p_stream->p_sys);
    return me->Send(reinterpret_cast<AbstractStream *>(id), p_block);
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
