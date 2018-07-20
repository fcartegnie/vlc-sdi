#ifndef SDIOUTPUT_HPP
#define SDIOUTPUT_HPP

#include <vlc_common.h>

namespace sdi_sout
{
    class AbstractStream;

    class SDIOutput
    {
        public:
            SDIOutput(sout_stream_t *);
            virtual ~SDIOutput();
            virtual int Open() = 0;
            virtual int Process() = 0;
            virtual AbstractStream *Add(const es_format_t *);
            virtual int   Send(AbstractStream *, block_t *);
            virtual void  Del(AbstractStream *);
            virtual int   Control(int, va_list);

        protected:
            sout_stream_t *p_stream;

        private:
            static void *SoutCallback_Add(sout_stream_t *, const es_format_t *);
            static void  SoutCallback_Del(sout_stream_t *, void *);
            static int   SoutCallback_Send(sout_stream_t *, void *, block_t*);
            static int   SoutCallback_Control(sout_stream_t *, int, va_list);
            static void  SoutCallback_Flush(sout_stream_t *, void *);
    };
}

#endif
