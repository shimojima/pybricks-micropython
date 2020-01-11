// SPDX-License-Identifier
// Copyright (c) 2018-2019 Laurens Valk
// Copyright (c) 2019 David Lechner

// class Speaker
//
// For creating sounds on ev3dev.
//
// There are two ways to create sounds. One is to use the "Beep" device to
// create tones with a given frequency. This is done using the Linux input
// device so that the sound is played on the EV3. The other is to use ALSA
// for PCM playback of sampled sounds. To keep the code simple, we just
// invoke `aplay` in a subprocess (same with espeak for text to speech).

#include <errno.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stdio.h>

#include <linux/input.h>
#include <sys/stat.h>
#include <sys/types.h>

#include <gio/gio.h>
#include <glib.h>

#include "py/mpconfig.h"
#include "py/mphal.h"
#include "py/obj.h"
#include "py/runtime.h"

#include "pb_ev3dev_types.h"
#include "pbkwarg.h"
#include "pbobj.h"

#define EV3DEV_EV3_INPUT_DEV_PATH "/dev/input/by-path/platform-sound-event"

typedef struct _ev3dev_Speaker_obj_t {
    mp_obj_base_t base;
    bool intialized;
    int beep_fd;
    gboolean aplay_busy;
    gboolean aplay_result;
    GError *aplay_error;
    gboolean espeak_busy;
    gboolean espeak_result;
    GError *espeak_error;
    gboolean splice_busy;
    gssize splice_result;
    GError *splice_error;
} ev3dev_Speaker_obj_t;

STATIC ev3dev_Speaker_obj_t ev3dev_speaker_singleton;


STATIC mp_obj_t ev3dev_Speaker_make_new(const mp_obj_type_t *type, size_t n_args, size_t n_kw, const mp_obj_t *args) {
    ev3dev_Speaker_obj_t *self = &ev3dev_speaker_singleton;
    if (!self->intialized) {
        self->base.type = &pb_type_ev3dev_Speaker;
        self->beep_fd = open(EV3DEV_EV3_INPUT_DEV_PATH, O_RDWR, 0);
        if (self->beep_fd == -1) {
            perror("Failed to open input dev for sound, beep will not work");
        }
        self->intialized = true;
    }
    return MP_OBJ_FROM_PTR(self);
}

static int set_beep_frequency(ev3dev_Speaker_obj_t *self, int32_t freq) {
    struct input_event event = {
        .type = EV_SND,
        .code = SND_TONE,
        .value = freq,
    };
    int ret;

    do {
        ret = write(self->beep_fd, &event, sizeof(event));
    } while (ret == -1 && errno == EINTR);

    return ret;
}

STATIC mp_obj_t ev3dev_Speaker_beep(size_t n_args, const mp_obj_t *pos_args, mp_map_t *kw_args) {
    PB_PARSE_ARGS_METHOD(n_args, pos_args, kw_args,
        ev3dev_Speaker_obj_t, self,
        PB_ARG_DEFAULT_INT(frequency, 500),
        PB_ARG_DEFAULT_INT(duration, 100)
    );

    mp_int_t freq = pb_obj_get_int(frequency);
    mp_int_t ms = pb_obj_get_int(duration);

    int ret = set_beep_frequency(self, freq);
    if (ret == -1) {
        mp_raise_OSError(errno);
    }

    if (ms < 0) {
        return mp_const_none;
    }

    nlr_buf_t nlr;
    if (nlr_push(&nlr) == 0) {
        mp_hal_delay_ms(ms);
        set_beep_frequency(self, 0);
        nlr_pop();
    }
    else {
        set_beep_frequency(self, 0);
        nlr_jump(nlr.ret_val);
    }

    return mp_const_none;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_KW(ev3dev_Speaker_beep_obj, 1, ev3dev_Speaker_beep);

STATIC void ev3dev_Speaker_play_note(ev3dev_Speaker_obj_t *self, mp_obj_t obj, int duration) {
    const char *note = mp_obj_str_get_str(obj);
    int pos = 0;
    double freq;
    bool release = true;

    // Note names can be A-G followed by optional # (sharp) or b (flat) or R for rest
    switch (note[pos++]) {
    case 'C':
        switch (note[pos++]) {
        case 'b':
            mp_raise_ValueError("'Cb' is not allowed");
            break;
        case '#':
            freq = 17.32;
            break;
        default:
            pos--;
            freq = 16.35;
            break;
        }
        break;
    case 'D':
        switch (note[pos++]) {
        case 'b':
            freq = 17.32;
            break;
        case '#':
            freq = 19.45;
            break;
        default:
            pos--;
            freq = 18.35;
            break;
        }
        break;
    case 'E':
        switch (note[pos++]) {
        case 'b':
            freq = 19.45;
            break;
        case '#':
            mp_raise_ValueError("'E#' is not allowed");
            break;
        default:
            pos--;
            freq = 20.60;
            break;
        }
        break;
    case 'F':
        switch (note[pos++]) {
        case 'b':
            mp_raise_ValueError("'Fb' is not allowed");
            break;
        case '#':
            freq = 23.12;
            break;
        default:
            pos--;
            freq = 21.83;
            break;
        }
        break;
    case 'G':
        switch (note[pos++]) {
        case 'b':
            freq = 23.12;
            break;
        case '#':
            freq = 25.96;
            break;
        default:
            pos--;
            freq = 24.50;
            break;
        }
        break;
    case 'A':
        switch (note[pos++]) {
        case 'b':
            freq = 25.96;
            break;
        case '#':
            freq = 29.14;
            break;
        default:
            pos--;
            freq = 27.50;
            break;
        }
        break;
    case 'B':
        switch (note[pos++]) {
        case 'b':
            freq = 29.14;
            break;
        case '#':
            mp_raise_ValueError("'B#' is not allowed");
            break;
        default:
            pos--;
            freq = 30.87;
            break;
        }
        break;
    case 'R':
        freq = 0;
        break;
    default:
        mp_raise_ValueError("Missing note name A-G or R");
        break;
    }

    // Note name must be followed by the octave number
    if (freq != 0) {
        int octave = note[pos++] - '0';
        if (octave < 2 || octave > 8) {
            mp_raise_ValueError("Missing octave number 2-8");
        }
        freq *= 2 << octave;
    }

    // '/' delimiter is required between octave and fraction
    if (note[pos++] != '/') {
        mp_raise_ValueError("Missing '/'");
    }

    // The fractional size of the note, e.g. 4 = quarter note
    int fraction = note[pos++] - '0';
    if (fraction < 0 || fraction > 9) {
        mp_raise_ValueError("Missing fractional value 1, 2, 4, 8, etc.");
    }

    // optional second digit
    int fraction2 = note[pos++] - '0';
    if (fraction2 < 0 || fraction2 > 9) {
        pos--;
    }
    else {
        fraction = fraction * 10 + fraction2;
    }

    duration /= fraction;

    // optional decorations

    if (note[pos++] == '.') {
        // dotted note has length extended by 1/2
        duration = 3 * duration / 2;
    }
    else {
        pos--;
    }

    if (note[pos++] == '_') {
        // note with tie/slur is not released
        release = false;
    }
    else {
        pos--;
    }

    set_beep_frequency(self, (int)freq);

    // Normally, we want there to be a period of no sound (release) so that
    // notes are distinct instead of running together. To sound good, the
    // release period is made proportional to duration of the note.
    if (release) {
        mp_hal_delay_ms(7 * duration / 8);
        set_beep_frequency(self, 0);
        mp_hal_delay_ms(duration / 8);
    }
    else {
        mp_hal_delay_ms(duration);
    }
}

STATIC mp_obj_t ev3dev_Speaker_play_notes(size_t n_args, const mp_obj_t *pos_args, mp_map_t *kw_args) {
    PB_PARSE_ARGS_METHOD(n_args, pos_args, kw_args,
        ev3dev_Speaker_obj_t, self,
        PB_ARG_REQUIRED(notes),
        PB_ARG_DEFAULT_INT(tempo, 120)
    );

    // length of whole note in milliseconds = 4 quarter/whole * 60 s/min * 1000 ms/s / tempo quarter/min
    int duration = 4 * 60 * 1000 / pb_obj_get_int(tempo);

    nlr_buf_t nlr;
    mp_obj_t item;
    mp_obj_t iterable = mp_getiter(notes, NULL);
    if (nlr_push(&nlr) == 0) {
        while ((item = mp_iternext(iterable)) != MP_OBJ_STOP_ITERATION) {
            ev3dev_Speaker_play_note(self, item, duration);
        }
        // in case the last note has '_'
        set_beep_frequency(self, 0);
        nlr_pop();
    }
    else {
        // ensure that sound stops if an exception is raised
        set_beep_frequency(self, 0);
        nlr_jump(nlr.ret_val);
    }

    return mp_const_none;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_KW(ev3dev_Speaker_play_notes_obj, 1, ev3dev_Speaker_play_notes);

STATIC void ev3dev_Speaker_aplay_callback(GObject *source_object, GAsyncResult *res, gpointer user_data) {
    GSubprocess *subprocess = G_SUBPROCESS(source_object);
    ev3dev_Speaker_obj_t *self = user_data;
    g_clear_error(&self->aplay_error);
    self->aplay_result = g_subprocess_wait_check_finish(subprocess, res, &self->aplay_error);
    self->aplay_busy = FALSE;
}

STATIC mp_obj_t ev3dev_Speaker_play_file(size_t n_args, const mp_obj_t *pos_args, mp_map_t *kw_args) {
    PB_PARSE_ARGS_METHOD(n_args, pos_args, kw_args,
        ev3dev_Speaker_obj_t, self,
        PB_ARG_REQUIRED(file)
    );

    const char *path = mp_obj_str_get_str(file);

    // FIXME: This function needs to be protected agains re-entrancy to make it
    // thread-safe.

    GError *error = NULL;
    GSubprocess *aplay = g_subprocess_new(
        G_SUBPROCESS_FLAGS_STDOUT_SILENCE | G_SUBPROCESS_FLAGS_STDERR_PIPE,
        &error, "aplay", "-q", path, NULL);
    if (!aplay) {
        // This error is unexpected, so doesn't need to be "user-friendly"
        mp_obj_t ex = mp_obj_new_exception_msg_varg(&mp_type_RuntimeError,
            "Failed to spawn aplay: %s", error->message);
        g_error_free(error);
        nlr_raise(ex);
    }

    self->aplay_busy = TRUE;
    g_subprocess_wait_check_async(aplay, NULL, ev3dev_Speaker_aplay_callback, self);

    // Play sound in non-blocking fashion. If an exception occurs during playback,
    // we have to keep running the event loop until the async function has completed.
    // This means there is small chance that multiple exceptions could be caught
    // and only the last one will be re-raised.
    gboolean exception = FALSE;
    nlr_buf_t nlr;
    do {
        if (nlr_push(&nlr) == 0) {
            MICROPY_EVENT_POLL_HOOK
            nlr_pop();
        } else {
            g_subprocess_force_exit(aplay);
            exception = TRUE;
        }
    } while (self->aplay_busy);

    if (exception) {
        g_object_unref(aplay);
        nlr_jump(nlr.ret_val);
    }

    if (!self->aplay_result) {
        const char *err_msg = self->aplay_error->message;

        // If there is something in stderr, use that as the error message instead
        // of error->message
        GInputStream *stderr_stream = g_subprocess_get_stderr_pipe(aplay);
        gchar stderr_bytes[4096];
        gsize bytes_read;
        if (g_input_stream_read_all(stderr_stream, stderr_bytes, sizeof(stderr_bytes), &bytes_read, NULL, NULL)) {
            if (bytes_read) {
                err_msg = stderr_bytes;
                stderr_bytes[bytes_read] = '\0'; // just in case
            }
        }

        mp_obj_t ex = mp_obj_new_exception_msg_varg(&mp_type_RuntimeError,
            "Playing file failed: %s", err_msg);
        g_object_unref(aplay);
        nlr_raise(ex);
    }

    g_object_unref(aplay);

    return mp_const_none;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_KW(ev3dev_Speaker_play_file_obj, 1, ev3dev_Speaker_play_file);

STATIC void ev3dev_Speaker_espeak_callback(GObject *source_object, GAsyncResult *res, gpointer user_data) {
    GSubprocess *subprocess = G_SUBPROCESS(source_object);
    ev3dev_Speaker_obj_t *self = user_data;
    g_clear_error(&self->espeak_error);
    self->espeak_result = g_subprocess_wait_check_finish(subprocess, res, &self->espeak_error);
    self->espeak_busy = FALSE;
}

STATIC void ev3dev_Speaker_splice_callback(GObject *source_object, GAsyncResult *res, gpointer user_data) {
    GOutputStream *out_stream = G_OUTPUT_STREAM(source_object);
    ev3dev_Speaker_obj_t *self = user_data;
    g_clear_error(&self->splice_error);
    self->splice_result = g_output_stream_splice_finish(out_stream, res, &self->splice_error);
    self->splice_busy = FALSE;
}

STATIC mp_obj_t ev3dev_Speaker_say(size_t n_args, const mp_obj_t *pos_args, mp_map_t *kw_args) {
    PB_PARSE_ARGS_METHOD(n_args, pos_args, kw_args,
        ev3dev_Speaker_obj_t, self,
        PB_ARG_REQUIRED(text)
    );

    const char *text_ = mp_obj_str_get_str(text);

    // FIXME: This function needs to be protected agains re-entrancy to make it
    // thread-safe.

    GError *error = NULL;
    GSubprocess *espeak = g_subprocess_new(
        G_SUBPROCESS_FLAGS_STDOUT_PIPE | G_SUBPROCESS_FLAGS_STDERR_PIPE,
        &error, "espeak", "-a", "200", "-s", "100", "-v", "en", "--stdout", text_, NULL);
    if (!espeak) {
        // This error is unexpected, so doesn't need to be "user-friendly"
        mp_obj_t ex = mp_obj_new_exception_msg_varg(&mp_type_RuntimeError,
            "Failed to spawn espeak: %s", error->message);
        g_error_free(error);
        nlr_raise(ex);
    }

    GSubprocess *aplay = g_subprocess_new(
        G_SUBPROCESS_FLAGS_STDIN_PIPE | G_SUBPROCESS_FLAGS_STDOUT_SILENCE | G_SUBPROCESS_FLAGS_STDERR_PIPE,
        &error, "aplay", "-q", NULL);
    if (!aplay) {
        // This error is unexpected, so doesn't need to be "user-friendly"
        mp_obj_t ex = mp_obj_new_exception_msg_varg(&mp_type_RuntimeError,
            "Failed to spawn aplay: %s", error->message);
        g_error_free(error);
        g_object_unref(espeak);
        nlr_raise(ex);
    }

    self->espeak_busy = TRUE;
    g_subprocess_wait_check_async(espeak, NULL, ev3dev_Speaker_espeak_callback, self);
    self->aplay_busy = TRUE;
    g_subprocess_wait_check_async(aplay, NULL, ev3dev_Speaker_aplay_callback, self);
    self->splice_busy = TRUE;
    GOutputStream *out_stream = g_subprocess_get_stdin_pipe(aplay);
    g_output_stream_splice_async(out_stream, g_subprocess_get_stdout_pipe(espeak),
        G_OUTPUT_STREAM_SPLICE_CLOSE_SOURCE | G_OUTPUT_STREAM_SPLICE_CLOSE_TARGET,
        G_PRIORITY_DEFAULT, NULL, ev3dev_Speaker_splice_callback, self);

    // Play sound in non-blocking fashion. If an exception occurs during playback,
    // we have to keep running the event loop until the async function has completed.
    // This means there is small chance that multiple exceptions could be caught
    // and only the last one will be re-raised.
    gboolean exception = FALSE;
    nlr_buf_t nlr;
    do {
        if (nlr_push(&nlr) == 0) {
            MICROPY_EVENT_POLL_HOOK
            nlr_pop();
        } else {
            g_subprocess_force_exit(espeak);
            g_subprocess_force_exit(aplay);
            exception = TRUE;
        }
    } while (self->espeak_busy || self->aplay_busy || self->splice_busy);

    if (exception) {
        g_object_unref(aplay);
        g_object_unref(espeak);
        nlr_jump(nlr.ret_val);
    }

    if (!self->aplay_result) {
        const char *err_msg = self->aplay_error->message;

        // If there is something in stderr, use that as the error message instead
        // of error->message
        GInputStream *stderr_stream = g_subprocess_get_stderr_pipe(aplay);
        gchar stderr_bytes[4096];
        gsize bytes_read;
        if (g_input_stream_read_all(stderr_stream, stderr_bytes, sizeof(stderr_bytes), &bytes_read, NULL, NULL)) {
            if (bytes_read) {
                err_msg = stderr_bytes;
                stderr_bytes[bytes_read] = '\0'; // just in case
            }
        }

        mp_obj_t ex = mp_obj_new_exception_msg_varg(&mp_type_RuntimeError,
            "Saying text failed: %s", err_msg);
        g_object_unref(aplay);
        g_object_unref(espeak);
        nlr_raise(ex);
    }

    if (!self->espeak_result) {
        const char *err_msg = self->espeak_error->message;

        // If there is something in stderr, use that as the error message instead
        // of error->message
        GInputStream *stderr_stream = g_subprocess_get_stderr_pipe(espeak);
        gchar stderr_bytes[4096];
        gsize bytes_read;
        if (g_input_stream_read_all(stderr_stream, stderr_bytes, sizeof(stderr_bytes), &bytes_read, NULL, NULL)) {
            if (bytes_read) {
                err_msg = stderr_bytes;
                stderr_bytes[bytes_read] = '\0'; // just in case
            }
        }

        mp_obj_t ex = mp_obj_new_exception_msg_varg(&mp_type_RuntimeError,
            "Saying text failed: %s", err_msg);
        g_object_unref(aplay);
        g_object_unref(espeak);
        nlr_raise(ex);
    }

    g_object_unref(aplay);
    g_object_unref(espeak);

    return mp_const_none;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_KW(ev3dev_Speaker_say_obj, 1, ev3dev_Speaker_say);

STATIC const mp_rom_map_elem_t ev3dev_Speaker_locals_dict_table[] = {
    { MP_ROM_QSTR(MP_QSTR_beep),        MP_ROM_PTR(&ev3dev_Speaker_beep_obj)        },
    { MP_ROM_QSTR(MP_QSTR_play_notes),  MP_ROM_PTR(&ev3dev_Speaker_play_notes_obj)  },
    { MP_ROM_QSTR(MP_QSTR_play_file),   MP_ROM_PTR(&ev3dev_Speaker_play_file_obj)   },
    { MP_ROM_QSTR(MP_QSTR_say),         MP_ROM_PTR(&ev3dev_Speaker_say_obj)         },
};
STATIC MP_DEFINE_CONST_DICT(ev3dev_Speaker_locals_dict, ev3dev_Speaker_locals_dict_table);

const mp_obj_type_t pb_type_ev3dev_Speaker = {
    { &mp_type_type },
    .name = MP_QSTR_Speaker,
    .make_new = ev3dev_Speaker_make_new,
    .locals_dict = (mp_obj_dict_t*)&ev3dev_Speaker_locals_dict,
};