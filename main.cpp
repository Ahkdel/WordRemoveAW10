/* GStreamer
 * Copyright (C) 2000,2001,2002,2003,2005
 *           Thomas Vander Stichele <thomas at apestaart dot org>
 */

#include "WordRemove.h"

int main (int argc, char *argv[])
{
    gst_init (&argc, &argv);
    g_print("PID: %d\n", getpid());
    signal(SIGRTMIN+2, wakeup);
    SMR_dB = atof(argv[1]);
    g_print("SMR_dB: %f\n", SMR_dB);

    GstBus *bus;
    PipeStruct pipe;

    //Element creations
    newElements(&pipe);

    //Pipe construction
    gst_bin_add_many(GST_BIN (pipe.pipeline), pipe.alsasrc, pipe.decodebin, pipe.audioresample,
                     pipe.audioconvert0, pipe.level, pipe.queue, pipe.fakesink, pipe.flacenc0, NULL);
    if (!setLinks(&pipe)) return -1;
    pipe.filesink0 = NULL;

    //Properties of elements
    setProperties(&pipe);

    //SignalS for decodebin
    g_signal_connect(G_OBJECT(pipe.decodebin), "pad_added", G_CALLBACK (decoder_pad_handler), &pipe);
    g_signal_connect(G_OBJECT(pipe.decodebin), "drained",   G_CALLBACK (drained_callback),    &pipe);

    loop = g_main_loop_new(NULL, FALSE);
    g_print("Initialized the loop\n");

    bus = gst_element_get_bus (pipe.pipeline);
    gst_bus_add_signal_watch(bus);
    g_signal_connect(G_OBJECT(bus), "message", G_CALLBACK (message_cb), &pipe);

    GstStateChangeReturn returnstate = gst_element_set_state(pipe.pipeline, GST_STATE_PLAYING);
    g_print("returnstate in main = %d\n", returnstate);
    if (returnstate == GST_STATE_CHANGE_FAILURE) {
        g_printerr ("(Before loop): Unable to set pipeline to PAUSED. Executing.\n");
        gst_object_unref (pipe.pipeline);
        return -1;
    }
    g_timeout_add(300, GSourceFunc(timeout_sleep_cb), &pipe);
    g_print("Running the loop\n");
    g_main_loop_run(loop);

    //Free resources
    gst_bus_remove_signal_watch(bus);
    gst_object_unref(bus);
    g_main_loop_unref (loop);

    gst_element_set_state (pipe.pipeline, GST_STATE_NULL);
    gst_object_unref (pipe.pipeline);
    return 0;
}

static void
message_cb (GstBus *bus, GstMessage *msg, PipeStruct *pipe) {
    msg = gst_bus_timed_pop_filtered (bus, GST_CLOCK_TIME_NONE,
                                     (GstMessageType)(GST_MESSAGE_STATE_CHANGED | GST_MESSAGE_ERROR | GST_MESSAGE_EOS | GST_MESSAGE_ELEMENT | GST_MESSAGE_STREAM_STATUS));
    // Parse message
    if (msg != NULL) {
        //g_print("-> Message got: %d\n", GST_MESSAGE_TYPE(msg));
        switch (GST_MESSAGE_TYPE (msg))
        {
        case GST_MESSAGE_ERROR:
            GError *err;
            gchar *debug_info;
            gst_message_parse_error (msg, &err, &debug_info);
            g_printerr ("Error received from element %s: %s\n", GST_OBJECT_NAME (msg->src), err->message);
            g_printerr ("Debugging information: %s\n", debug_info ? debug_info : "none");
            g_clear_error (&err);
            g_free (debug_info);
            g_main_loop_quit(loop);
            break;

        case GST_MESSAGE_EOS:
            if (GST_MESSAGE_SRC (msg) == GST_OBJECT (pipe->filesink0))
                g_print ("End-Of-Stream reached.\n");
            g_main_loop_quit(loop);
            break;

        case GST_MESSAGE_STATE_CHANGED:
            // We are only interested in state-changed messages from the pipeline
            if (GST_MESSAGE_SRC (msg) == GST_OBJECT (pipe->pipeline)) {
                GstState old_state, new_state, pending_state;
                gst_message_parse_state_changed (msg, &old_state, &new_state, &pending_state);
                const gchar *stateOld = gst_element_state_get_name (old_state);
                const gchar *stateNew = gst_element_state_get_name (new_state);
                g_print("Pipeline state changed from %s to %s:\n", stateOld, stateNew);
            }
            break;

        case GST_MESSAGE_ELEMENT:
            //g_print("\npipe->pipeline->current_state: %d", pipe->pipeline->current_state);
            if ((recording) && (pipe->pipeline->current_state == GST_STATE_PLAYING)) {
                message_handler(msg, pipe);
                if (time_reserve > GUARDING_TIME) {
                    g_print("time_reserve > GUARDING_TIME\n");
                    GstPad *srcpad = gst_element_get_static_pad(pipe->queue, "src");
                    gst_pad_add_probe(srcpad, GstPadProbeType(GST_PAD_PROBE_TYPE_BLOCK),
                                      GstPadProbeCallback(sink_swap_cb), pipe, NULL);
                    g_print("Writing donefile\n");
                    writeDoneFile();
                    time_reserve = 0;
                    first_voice = TRUE;
                    counting = FALSE;
                    recording = FALSE;
                    awake = FALSE;
                    g_print("Adding timeout_sleep_cb\n");
                    g_timeout_add(300, GSourceFunc(timeout_sleep_cb), pipe);
                }
            }
            break;
        case GST_MESSAGE_STREAM_STATUS:
            //g_print("GST_MESSAGE_STREAM_STATUS\n");
            break;
        default:
            // We should not reach here
            g_printerr ("Unexpected message received.\n");
            break;

        }
        gst_message_unref (msg);
    }
}















/*
static void
info(char *dev_name, snd_pcm_stream_t stream) {
    snd_pcm_hw_params_t *hw_params;
    int err;
    snd_pcm_t *handle;
    unsigned int max;
    unsigned int min;
    unsigned int val;
    int dir;
    snd_pcm_uframes_t frames;
    if ((err = snd_pcm_open (&handle, dev_name, stream, 0)) < 0) {
        fprintf (stderr, "cannot open audio device %s (%s)\n",
                 dev_name,
                 snd_strerror (err));
        return;
    }
    if ((err = snd_pcm_hw_params_malloc (&hw_params)) < 0) {
        fprintf (stderr, "cannot allocate hardware parameter structure (%s)\n",
                 snd_strerror (err));
        exit (1);
    }
    if ((err = snd_pcm_hw_params_any (handle, hw_params)) < 0) {
        fprintf (stderr, "cannot initialize hardware parameter structure (%s)\n",
                 snd_strerror (err));
        exit (1);
    }
    if ((err = snd_pcm_hw_params_get_channels_max(hw_params, &max)) < 0) {
        fprintf (stderr, "cannot  (%s)\n",
                 snd_strerror (err));
        exit (1);
    }
    printf("max channels %d\n", max);
    if ((err = snd_pcm_hw_
    gchar *device, *device_name, *card_name;
    gint64 buffer_time;
    g_object_get(pipe.alsasrc, "actual-buffer-time", &buffer_time, "device", &device, "device-name", &device_name, "card-name", &card_name, NULL);
    g_print ("#############################\n");
    g_print ("#      ALSA PROPERTIES      #\n");
    g_print ("#############################\n");
    g_print ("device (only writable): %s\nactual-buffer-time: %lli\ndevice-name: %s\ncard-name: %s\n\n",
             device, buffer_time, device_name, card_name);
    info(device, SND_PCM_STREAM_CAPTURE);
params_get_channels_min(hw_params, &min)) < 0) {
        fprintf (stderr, "cannot get channel info  (%s)\n",
                 snd_strerror (err));
        exit (1);
    }
    printf("min channels %d\n", min);

//      if ((err = snd_pcm_hw_params_get_sbits(hw_params)) < 0) {
//          fprintf (stderr, "cannot get bits info  (%s)\n",
//                   snd_strerror (err));
//          exit (1);
//      }
//      printf("bits %d\n", err);

    if ((err = snd_pcm_hw_params_get_rate_min(hw_params, &val, &dir)) < 0) {
        fprintf (stderr, "cannot get min rate (%s)\n",
                 snd_strerror (err));
        exit (1);
    }
    printf("min rate %d hz\n", val);
    if ((err = snd_pcm_hw_params_get_rate_max(hw_params, &val, &dir)) < 0) {
        fprintf (stderr, "cannot get max rate (%s)\n",
                 snd_strerror (err));
        exit (1);
    }
    printf("max rate %d hz\n", val);
    if ((err = snd_pcm_hw_params_get_period_time_min(hw_params, &val, &dir)) < 0) {
        fprintf (stderr, "cannot get min period time  (%s)\n",
                 snd_strerror (err));
        exit (1);
    }
    printf("min period time %d usecs\n", val);
    if ((err = snd_pcm_hw_params_get_period_time_max(hw_params, &val, &dir)) < 0) {
        fprintf (stderr, "cannot  get max period time  (%s)\n",
                 snd_strerror (err));
        exit (1);
    }
    printf("max period time %d usecs\n", val);
    if ((err = snd_pcm_hw_params_get_period_size_min(hw_params, &frames, &dir)) < 0) {
        fprintf (stderr, "cannot  get min period size  (%s)\n",
                 snd_strerror (err));
        exit (1);
    }
    printf("min period size in frames %lu\n", frames);
    if ((err = snd_pcm_hw_params_get_period_size_max(hw_params, &frames, &dir)) < 0) {
        fprintf (stderr, "cannot  get max period size (%s)\n",
                 snd_strerror (err));
        exit (1);
    }
    printf("max period size in frames %lu\n", frames);
    if ((err = snd_pcm_hw_params_get_periods_min(hw_params, &val, &dir)) < 0) {
        fprintf (stderr, "cannot  get min periods  (%s)\n",
                 snd_strerror (err));
        exit (1);
    }
    printf("min periods per buffer %d\n", val);
    if ((err = snd_pcm_hw_params_get_periods_max(hw_params, &val, &dir)) < 0) {
        fprintf (stderr, "cannot  get min periods (%s)\n",
                 snd_strerror (err));
        exit (1);
    }
    printf("max periods per buffer %d\n", val);
    if ((err = snd_pcm_hw_params_get_buffer_time_min(hw_params, &val, &dir)) < 0) {
        fprintf (stderr, "cannot get min buffer time (%s)\n",
                 snd_strerror (err));
        exit (1);
    }
    printf("min buffer time %d usecs\n", val);
    if ((err = snd_pcm_hw_params_get_buffer_time_max(hw_params, &val, &dir)) < 0) {
        fprintf (stderr, "cannot get max buffer time  (%s)\n",
                 snd_strerror (err));
        exit (1);
    }
    printf("max buffer time %d usecs\n", val);
    if ((err = snd_pcm_hw_params_get_buffer_size_min(hw_params, &frames)) < 0) {
        fprintf (stderr, "cannot get min buffer size (%s)\n",
                 snd_strerror (err));
        exit (1);
    }
    printf("min buffer size in frames %lu\n", frames);
    if ((err = snd_pcm_hw_params_get_buffer_size_max(hw_params, &frames)) < 0) {
        fprintf (stderr, "cannot get max buffer size  (%s)\n",
                 snd_strerror (err));
        exit (1);
    }
    printf("max buffer size in frames %lu\n", frames);
}*/
