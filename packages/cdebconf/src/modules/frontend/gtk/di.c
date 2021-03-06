/*****************************************************************************
 *
 * cdebconf - An implementation of the Debian Configuration Management
 *            System
 *
 * cdebconf is (c) 2000-2007 Randolph Chung and others under the following
 * license.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright
 * notice, this list of conditions and the following disclaimer in the
 * documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHORS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 *****************************************************************************/

/** @file di.c
 * specific debian-installer bits of the GTK+ frontend
 */

#include "di.h"

#include <string.h>
#include <sys/types.h>
#include <unistd.h>
#include <stdlib.h>
#include <syslog.h>

#include <gtk/gtk.h>
#include <gdk/gdkkeysyms.h>

#include "question.h"
#include "database.h"

#include "cdebconf_gtk.h"
#include "fe_data.h"
#include "ui.h"

/** Private data for d-i specific bits. */
struct di_data {
    /** Previously known kepmap, taken from debian-installer/keymap. */
    char * previous_keymap;
    /** Previously known language, taken from debconf/language. */
    char * previous_language;
};

/** Get the current value for a given template.
 *
 * The caller needs to free the returned string.
 *
 * @param fe cdebconf frontend
 * @param template name of the template
 * @return a newly allocated string containing the value, or an empty string
 *         if the template was not found
 */
static gchar * get_question_value(struct frontend * fe, const char * template)
{
    struct question * question = fe->qdb->methods.get(fe->qdb, template);
    const char * result = NULL;

    if (NULL != question) {
        result = question_getvalue(question, "" /* no language */);
    }
    if (NULL == result) {
        result = "";
    }
    return g_strdup(result);
}

/** Print the given string to the "syslog".
 *
 * @param string string to print
 */
static void print_to_syslog(const gchar * string)
{
    syslog(LOG_USER | LOG_DEBUG, "%s", string);
}

/** Get a statically allocated string corresponding to the given GTK+
 * log level.
 *
 * @param log_level the log level
 * @return a statically allocated string corresponding to the log level
 */
static const char * get_prefix(GLogLevelFlags log_level)
{
    switch (log_level & G_LOG_LEVEL_MASK) {
        case G_LOG_LEVEL_ERROR: return "ERROR";
        case G_LOG_LEVEL_CRITICAL: return "CRITICAL";
        case G_LOG_LEVEL_WARNING: return "WARNING";
        case G_LOG_LEVEL_MESSAGE: return "Message";
        case G_LOG_LEVEL_INFO: return "INFO";
        case G_LOG_LEVEL_DEBUG: return "DEBUG";
        default: return "(unknown)";
    }
}

/** Implementation of GLogFunc for the GTK+ frontend.
 *
 * This will log messages going through the glib log system to the standard
 * syslog.
 *
 * @param log_domain the log domain of the message
 * @param log_level the log level of the message
 * @param message the message to process
 * @param user_data user data, set in g_log_set_handler()
 */
static void log_glib_to_syslog(const gchar * log_domain,
                               GLogLevelFlags log_level, gchar const * message,
                               gpointer user_data)
{
    GString * gstring;
    gchar * string;

    gstring = g_string_new(NULL);
    g_string_append_printf(gstring, "cdebconf_gtk ");
    g_string_append_printf(gstring, "(process:%lu): ", (gulong) getpid());
    if (NULL != log_domain) {
        g_string_append_printf(gstring, "%s - ", log_domain);
    }
    g_string_append_printf(gstring, "%s: ", get_prefix(log_level));
    g_string_append(gstring, message);
    string = g_string_free(gstring, FALSE);
    print_to_syslog(string);
    g_free(string);
}

/** Implementation of GLogFunc for the GTK+ frontend.
 *
 * This will log messages going through the glib log system to the standard
 * syslog.
 *
 * @param log_domain the log domain of the message
 * @param log_level the log level of the message
 * @param message the message to process
 * @param user_data user data, set in g_log_set_handler()
 */
static GLogWriterOutput log_glib_structured_to_syslog(GLogLevelFlags log_level,
                                                      const GLogField *fields,
                                                      gsize n_fields,
                                                      gpointer user_data)
{
    const char *domain = NULL;
    const char *message = "(no message)";
    gsize i;

    for (i = 0; i < n_fields; i++) {
          const GLogField *field = &fields[i];

          if (g_strcmp0(field->key, "MESSAGE") == 0) {
              message = field->value;
          }
          else if (g_strcmp0(field->key, "GLIB_DOMAIN") == 0) {
              domain = field->value;
          }
    }

    log_glib_to_syslog(domain, log_level, message, NULL);
    return G_LOG_WRITER_HANDLED;
}

/** Make the given window fullscreen.
 *
 * @param window main window
 */
static void make_fullscreen(GtkWidget * window)
{
    GdkScreen * screen;

    screen = gtk_window_get_screen(GTK_WINDOW(window));
    gtk_widget_set_size_request(window, gdk_screen_get_width(screen),
                                gdk_screen_get_height(screen));
    gtk_window_fullscreen(GTK_WINDOW(window));
}

/** Increase or decrease font size
 * @param factor (> 1 to increase, < 1 to decrease)
 */
static void di_change_font_size(struct frontend *fe, float factor)
{
    GtkSettings *gsettings;
    char *font_name;
    char *size_s, *end;
    char *sed;
    long size, newsize;
    FILE *zoom;
    const char *zoom_path = "/var/lib/cdebconf/zoom";
    unsigned old_factor;

    gsettings = gtk_settings_get_default ();
    g_object_get(gsettings, "gtk-font-name", &font_name, NULL);
    if (!font_name)
        return;

    size_s = strpbrk(font_name, "0123456789");
    if (!size_s) {
        g_free(font_name);
        return;
    }
    size = strtol(size_s, &end, 10);
    if (end == size_s) {
        g_free(font_name);
        return;
    }

    newsize = size * factor;
    if (newsize == size) {
        if (factor < 1) {
            newsize = size - 1;
        } else
            newsize = size + 1;
    }
    if (newsize <= 0)
        newsize = 1;

    zoom = fopen(zoom_path, "r");
    if (!zoom)
        old_factor = 100;
    else {
        if (fscanf(zoom, "%u", &old_factor) != 1)
            old_factor = 100;
        fclose(zoom);
    }

    zoom = fopen(zoom_path, "w");
    if (!zoom)
        return;
    fprintf(zoom, "%u\n", (unsigned) (old_factor * factor));
    fclose(zoom);

    asprintf(&sed, "sed -i 's/^gtk-font-name.*$/gtk-font-name = \"%.*s%d%s\"/' "
            "/etc/gtk-2.0/gtkrc",
            (int) (size_s - font_name), font_name, (int) newsize, end);
    system(sed);
    free(sed);
    g_free(font_name);

    gtk_rc_reparse_all_for_settings(gsettings, TRUE);
    cdebconf_gtk_set_answer_notok(fe);
}

/** Key event handler implementing global key shortcuts.
 *
 * @param widget main window
 * @param key the pressed key
 * @param fe cdebconf frontend
 * @return TRUE if "Cancel" was handled, FALSE otherwise
 */
static gboolean di_shortcuts(GtkWidget * widget, GdkEventKey * key,
                             struct frontend * fe)
{
    if (GDK_KEY_ZoomIn == key->keyval ||
            ((GDK_KEY_plus == key->keyval
              || GDK_KEY_KP_Add == key->keyval)
                && GDK_CONTROL_MASK & key->state)) {
        di_change_font_size(fe, 1.25);
        return TRUE;
    }
    if (GDK_KEY_ZoomOut == key->keyval ||
            ((GDK_KEY_minus == key->keyval
              || GDK_KEY_KP_Subtract == key->keyval)
                && GDK_CONTROL_MASK & key->state)) {
        di_change_font_size(fe, 0.8);
        return TRUE;
    }
    return FALSE;
}

/** Add global keyboard shortcuts
 *
 * @param fe cdebconf frontend
 */
static void set_shortcuts(struct frontend *fe)
{
    struct frontend_data * fe_data = fe->data;
    cdebconf_gtk_add_global_key_handler(fe, fe_data->window, G_CALLBACK(di_shortcuts));
}

/** Setup d-i specific bits.
 *
 * This will create and initialize the relevant data structure.
 *
 * This will allow setup the glib log handler to log through standard
 * syslog.
 *
 * @param fe cdebconf frontend
 * @return FALSE if initialization failed
 * @see log_glib_to_syslog()
 */
gboolean cdebconf_gtk_di_setup(struct frontend * fe)
{
    struct frontend_data * fe_data = fe->data;
    struct di_data * di_data;
    GdkCursor * cursor;

    g_assert(NULL == fe_data->di_data);
    if (NULL == (di_data = g_malloc0(sizeof (struct di_data)))) {
        return FALSE;
    }
    di_data->previous_keymap = get_question_value(
        fe, "debian-installer/keymap");
    di_data->previous_language = get_question_value(fe, "debconf/language");

    fe_data->di_data = di_data;

    (void) g_set_printerr_handler(print_to_syslog);
    g_log_set_writer_func(log_glib_structured_to_syslog, NULL, NULL);

    make_fullscreen(fe_data->window);
    set_shortcuts(fe);

    cursor = gdk_cursor_new_for_display(gdk_display_get_default(), GDK_LEFT_PTR);
    gdk_window_set_cursor(gdk_get_default_root_window(), cursor);
    gdk_cursor_unref(cursor);

    return TRUE;
}

/** Returns the current text direction.
 *
 * @param fe cdebconf frontend
 * @return the current text direction
 */
static GtkTextDirection get_text_direction(struct frontend * fe)
{
    char * dirstr;
    GtkTextDirection direction;

    dirstr = cdebconf_gtk_get_text(fe, "debconf/text-direction",
                                   "LTR - default");

    if ('R' == dirstr[0]) {
        direction = GTK_TEXT_DIR_RTL;
    } else {
        direction = GTK_TEXT_DIR_LTR;
    }
    g_free(dirstr);
    return direction;
}

/** Update various settings with the current language settings.
 *
 * @param fe cdebconf frontend
 */
static void refresh_language(struct frontend * fe)
{
    /* This will enable different fonts to be used for different language. */
    gtk_rc_reparse_all();
    /* Adapt text direction. */
    gtk_widget_set_default_direction(get_text_direction(fe));
}

/** Update what needs to be updated on a new user interaction.
 *
 * This must be called after gdk_threads_enter().
 *
 * @param fe cdebconf frontend
 */
void cdebconf_gtk_di_run_dialog(struct frontend * fe)
{
    struct frontend_data * fe_data = fe->data;
    struct di_data * di_data = fe_data->di_data;
    char * keymap;
    char * language;

    g_assert(NULL != di_data);

    cdebconf_gtk_update_frontend_title(fe);
    keymap = get_question_value(fe, "debian-installer/keymap");
    if (0 != strcmp(keymap, di_data->previous_keymap)) {
        g_free(di_data->previous_keymap);
        di_data->previous_keymap = keymap;
    } else {
        g_free(keymap);
    }

    language = get_question_value(fe, "debconf/language");
    if (0 != strcmp(language, di_data->previous_language)) {
        refresh_language(fe);
        g_free(di_data->previous_language);
        di_data->previous_language = language;
    } else {
        g_free(language);
    }
}

/** Clean up d-i specific data structures.
 *
 * @param fe cdebconf frontend
 */
void cdebconf_gtk_di_shutdown(struct frontend * fe)
{
    struct frontend_data * fe_data = fe->data;
    struct di_data * di_data = fe_data->di_data;

    if (NULL != di_data) {
        fe_data->di_data = NULL;
        if (NULL != di_data->previous_keymap) {
            g_free(di_data->previous_keymap);
        }
        if (NULL != di_data->previous_language) {
            g_free(di_data->previous_language);
        }
        g_free(di_data);
    }
}

/* vim: et sw=4 si
 */
