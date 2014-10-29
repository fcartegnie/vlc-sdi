/*****************************************************************************
 * file.c: file logger plugin
 *****************************************************************************
 * Copyright (C) 2002-2008 the VideoLAN team
 * Copyright © 2007-2015 Rémi Denis-Courmont
 *
 * Authors: Samuel Hocevar <sam@zoy.org>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston MA 02110-1301, USA.
 *****************************************************************************/

#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#define VLC_MODULE_LICENSE VLC_LICENSE_GPL_2_PLUS
#include <vlc_common.h>
#include <vlc_plugin.h>
#include <vlc_fs.h>
#include <vlc_threads.h>
//#include <vlc_charset.h>

#include <stdarg.h>
#include <assert.h>
#include <errno.h>
#include <signal.h>

static const char msg_type[4][9] = { "", " error", " warning", " debug" };

typedef struct
{
    FILE *stream;
    const char *footer;
    int verbosity;
    mtime_t i_start;
    char *filename;
    vlc_thread_t thread;
    vlc_mutex_t lock;
    vlc_object_t *p_obj;
} vlc_logger_sys_t;

#define TEXT_FILENAME "vlc-log.txt"
#define TEXT_HEADER "\xEF\xBB\xBF" /* UTF-8 BOM */ \
                    "-- logger module started --\n"
#define TEXT_FOOTER "-- logger module stopped --\n"

static void *Run(void *data)
{
    vlc_logger_sys_t *p_sys = (vlc_logger_sys_t *)data;
    sigset_t set;
    sigemptyset(&set);
    sigaddset(&set, SIGHUP);
    int sig;
    for (;;) {
        if (sigwait(&set, &sig) != 0) {
            if (errno != EINTR)
                msg_Err(p_sys->p_obj, "sigwait failed: %m");
            continue;
        }

        if (sig == SIGHUP) {
            vlc_mutex_lock(&p_sys->lock);
            fclose( p_sys->stream );
            p_sys->stream = vlc_fopen( p_sys->filename, "at" );
            if( p_sys->stream == NULL )
            {
                msg_Err( p_sys->p_obj, "error opening logfile `%s': %m", p_sys->filename);
            }
            vlc_mutex_unlock(&p_sys->lock);
        } else
            msg_Err(p_sys->p_obj, "Unexpected signal %d", sig);
    }
    return NULL;
}

static void LogText(void *opaque, int type, const vlc_log_t *meta,
                    const char *format, va_list ap)
{
    vlc_logger_sys_t *sys = opaque;

    FILE *stream = sys->stream;

    if (sys->verbosity < type)
        return;

    mtime_t now = mdate() - sys->i_start;

    vlc_mutex_lock(&sys->lock);
    int canc = vlc_savecancel();
    flockfile(stream);
    fprintf(stream, "[%6"PRId64".%.6"PRId64"] ", now / CLOCK_FREQ, now % CLOCK_FREQ);
    fprintf(stream, "%s%s: ", meta->psz_module, msg_type[type]);
    vfprintf(stream, format, ap);
    putc_unlocked('\n', stream);
    funlockfile(stream);
    vlc_mutex_unlock(&sys->lock);
    vlc_restorecancel( canc );
}

#define HTML_FILENAME "vlc-log.html"
#define HTML_HEADER \
    "<!DOCTYPE html PUBLIC \"-//W3C//DTD HTML 4.01//EN\"\n" \
    "  \"http://www.w3.org/TR/html4/strict.dtd\">\n" \
    "<html>\n" \
    "  <head>\n" \
    "    <title>vlc log</title>\n" \
    "    <meta http-equiv=\"Content-Type\"" \
             " content=\"text/html; charset=UTF-8\">\n" \
    "  </head>\n" \
    "  <body style=\"background-color: #000000; color: #aaaaaa;\">\n" \
    "    <pre>\n" \
    "      <strong>-- logger module started --</strong>\n"
#define HTML_FOOTER \
    "      <strong>-- logger module stopped --</strong>\n" \
    "    </pre>\n" \
    "  </body>\n" \
    "</html>\n"

static void LogHtml(void *opaque, int type, const vlc_log_t *meta,
                    const char *format, va_list ap)
{
    static const unsigned color[4] = {
        0xffffff, 0xff6666, 0xffff66, 0xaaaaaa,
    };
    vlc_logger_sys_t *sys = opaque;
    FILE *stream = sys->stream;

    if (sys->verbosity < type)
        return;

    mtime_t now = mdate() - sys->i_start;

    vlc_mutex_lock(&sys->lock);
    int canc = vlc_savecancel();
    flockfile(stream);
    fprintf(stream, "[%6"PRId64".%.6"PRId64"] ", now / CLOCK_FREQ, now % CLOCK_FREQ);
    fprintf(stream, "%s%s: <span style=\"color: #%06x\">",
            meta->psz_module, msg_type[type], color[type]);
    /* FIXME: encode special ASCII characters */
    vfprintf(stream, format, ap);
    fputs("</span>\n", stream);
    funlockfile(stream);
    vlc_mutex_unlock(&sys->lock);
    vlc_restorecancel( canc );
}

static vlc_log_cb Open(vlc_object_t *obj, void **restrict sysp)
{
    if (!var_InheritBool(obj, "file-logging"))
        return NULL;

    int verbosity = var_InheritInteger(obj, "log-verbose");
    if (verbosity == -1)
        verbosity = var_InheritInteger(obj, "verbose");
    if (verbosity < 0)
        return NULL; /* nothing to log */

    verbosity += VLC_MSG_ERR;

    vlc_logger_sys_t *sys = malloc(sizeof (*sys));
    if (unlikely(sys == NULL))
        return NULL;

    const char *filename = TEXT_FILENAME;
    const char *header = TEXT_HEADER;

    vlc_log_cb cb = LogText;
    sys->filename = NULL;
    sys->footer = TEXT_FOOTER;
    sys->verbosity = verbosity;
    sys->i_start = mdate();
    sys->p_obj = obj;

    char *mode = var_InheritString(obj, "logmode");
    if (mode != NULL)
    {
        if (!strcmp(mode, "html"))
        {
            filename = HTML_FILENAME;
            header = HTML_HEADER;
            cb = LogHtml;
            sys->footer = HTML_FOOTER;
        }
        else if (strcmp(mode, "text"))
            msg_Warn(obj, "invalid log mode \"%s\"", mode);
        free(mode);
    }

    char *path = var_InheritString(obj, "logfile");
#ifdef __APPLE__
    if (path == NULL)
    {
        char *home = config_GetUserDir(VLC_HOME_DIR);
        if (home != NULL)
        {
            if (asprintf(&path, "%s/Library/Logs/%s", home, path) == -1)
                path = NULL;
            free(home);
        }
    }
#endif
    if (path != NULL)
        filename = path;

    sys->filename = strdup(filename);
    free(path);
    if(!sys->filename)
    {
        free(sys);
        return NULL;
    }

    /* Open the log file and remove any buffering for the stream */
    msg_Dbg(obj, "opening logfile `%s'", sys->filename);
    sys->stream = vlc_fopen(sys->filename, "at");
    if (sys->stream == NULL)
    {
        msg_Err(obj, "error opening log file `%s': %s", sys->filename,
                vlc_strerror_c(errno) );
        free(sys->filename);
        free(sys);
        return NULL;
    }
    setvbuf(sys->stream, NULL, _IONBF, 0);
    fputs(header, sys->stream);

    vlc_mutex_init(&sys->lock);
    if (vlc_clone(&sys->thread, Run, sys, VLC_THREAD_PRIORITY_LOW))
    {
        fclose(sys->stream);
        vlc_mutex_destroy(&sys->lock);
        free(sys->filename);
        free(sys);
    }

    *sysp = sys;

    return cb;
}

static void Close(void *opaque)
{
    vlc_logger_sys_t *sys = opaque;

    vlc_cancel(sys->thread);
    vlc_join(sys->thread, NULL);
    vlc_mutex_destroy(&sys->lock);

    fputs(sys->footer, sys->stream);
    fclose(sys->stream);

    free(sys->filename);
    free(sys);
}

static const char *const mode_list[] = { "text", "html" };
static const char *const mode_list_text[] = { N_("Text"), N_("HTML") };

#define FILE_LOG_TEXT N_("Log to file")
#define FILE_LOG_LONGTEXT N_("Log all VLC messages to a text file.")

#define LOGMODE_TEXT N_("Log format")
#define LOGMODE_LONGTEXT N_("Specify the logging format.")

#define LOGVERBOSE_TEXT N_("Verbosity")
#define LOGVERBOSE_LONGTEXT N_("Select the verbosity to use for log or -1 to " \
"use the same verbosity given by --verbose.")

vlc_module_begin()
    set_shortname(N_("Logger"))
    set_description(N_("File logger"))
    set_category(CAT_ADVANCED)
    set_subcategory(SUBCAT_ADVANCED_MISC)
    set_capability("logger", 15)
    set_callbacks(Open, Close)

    add_bool("file-logging", false, FILE_LOG_TEXT, FILE_LOG_LONGTEXT, false)
    add_savefile("logfile", NULL,
                 N_("Log filename"), N_("Specify the log filename."), false)
    add_string("logmode", "text", LOGMODE_TEXT, LOGMODE_LONGTEXT, false)
        change_string_list(mode_list, mode_list_text)
    add_integer("log-verbose", -1, LOGVERBOSE_TEXT, LOGVERBOSE_LONGTEXT,
                false)
vlc_module_end ()
