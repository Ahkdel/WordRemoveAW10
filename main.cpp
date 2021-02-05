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
    gchar *pathname = (char*) malloc(1), *length = (char*) malloc(3);
    time_t start;
    start = time(&start);
    do {
        GstBus *bus;
        PipeStruct pipe;
        first_voice = TRUE;

        //Element creations
        if (newElements(&pipe) < 0)
            return -1;

        //Pipe construction
        gst_bin_add_many(GST_BIN (pipe.pipeline), pipe.alsasrc, pipe.decodebin, pipe.audioresample,
                         pipe.level, pipe.selector, pipe.audioconvert0, pipe.flacenc0, pipe.filesink0, pipe.fakesink, NULL);

        //Properties of elements
        setProperties(&pipe);

        //Signal for decodebin
        g_signal_connect (pipe.decodebin, "pad_added", G_CALLBACK (decoder_pad_handler), &pipe);

        if (setLinks(&pipe) < 0)
            return -1;

        //Handling the selector. TODO: change to pads template (supposedly much faster)
        if (selector_pad_handler(&pipe) < 0) {
            gst_element_release_request_pad(pipe.selector, src0_pad);
            gst_element_release_request_pad(pipe.selector, src1_pad);
            g_print ("Error at linking output-selector to the rest of the elements. Executing...\n");
            return -1;
        }
//        g_print ("output-selector pads: \n`-> scr0: %s\n`-> src1: %s\n\n",
//                 gst_pad_get_name (src0_pad), gst_pad_get_name(src1_pad));
        g_object_set (G_OBJECT (pipe.selector), "active_pad", src1_pad, NULL);

        loop = g_main_loop_new(NULL, FALSE);
        g_print("Initialized the loop\n");

        bus = gst_element_get_bus (pipe.pipeline);
        gst_bus_add_signal_watch(bus);
        g_signal_connect(G_OBJECT (bus), "message", G_CALLBACK (message_cb), &pipe);

        time_reserve = 0;
        first_voice = TRUE;
        time_t now;
        g_print("Calling pause() and occured %f since beginning of program\n", difftime(time(&now), start));
        do pause();
        while (awake == FALSE);
        awake = TRUE;

        FILE *file = fopen("/home/root/WordRemove/status", "r");
        length = (char*) realloc(length,3);
        fscanf(file, "%s", length);
        pathname = (char*) realloc(pathname,atoi(length)+1);
        fscanf(file, "%s", pathname);
        g_object_set (G_OBJECT (pipe.filesink0), "location", pathname, NULL);
        g_print("Pathname: '%s'\n", pathname);
        //free(pathname);
        //free(length);
        fclose(file);

        GstStateChangeReturn returnstate = gst_element_set_state(pipe.pipeline, GST_STATE_PLAYING);

        if (returnstate == GST_STATE_CHANGE_FAILURE) {
            g_printerr ("Unable to set pipeline to PLAYING. Executing.\n");
            gst_object_unref (pipe.pipeline);
            return -1;
        }

        g_print("Running the loop\n");
        g_main_loop_run(loop);

        writeDoneFile();

        //Free resources
        gst_bus_remove_signal_watch(bus);
        gst_object_unref(bus);
        g_main_loop_unref (loop);

        gst_element_release_request_pad(pipe.selector, src0_pad);
        gst_element_release_request_pad(pipe.selector, src1_pad);
        gst_element_set_state (pipe.pipeline, GST_STATE_NULL);
        gst_object_unref (pipe.pipeline);
    } while(TRUE);
    return 0;
}

static gint
check_links (GstElement *element, gchar *pad) {
    GstPad *check_pad = gst_element_get_static_pad(element, pad);
    if (!check_pad){
        g_error("Couldn't retrieve the pad of %s\n", gst_pad_get_name(check_pad));
        g_object_unref(check_pad);
        return -1;
    }
    if (!gst_pad_is_linked(check_pad)){
        g_error("Pad (%s) is not linked\n", gst_pad_get_name(check_pad));
        g_object_unref(check_pad);
        return -1;
    }
    g_object_unref(check_pad);
    return 0;
}

static void
message_cb (GstBus *bus, GstMessage *msg, PipeStruct *pipe) {
    gboolean terminating = FALSE;
    msg = gst_bus_timed_pop_filtered (bus, GST_CLOCK_TIME_NONE,
                                      (GstMessageType)(GST_MESSAGE_STATE_CHANGED | GST_MESSAGE_ERROR | GST_MESSAGE_EOS | GST_MESSAGE_ELEMENT));
    // Parse message
    if (msg != NULL) {

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
            g_print ("End-Of-Stream reached.\n");
            g_main_loop_quit(loop);
            break;

        case GST_MESSAGE_STATE_CHANGED:
            // We are only interested in state-changed messages from the pipeline
            if (GST_MESSAGE_SRC (msg) == GST_OBJECT (pipe->pipeline)) {
                GstState old_state, new_state, pending_state;
                gst_message_parse_state_changed (msg, &old_state, &new_state, &pending_state);
                g_print ("Pipeline state changed from %s to %s:\n",
                         gst_element_state_get_name (old_state), gst_element_state_get_name (new_state));
//                gchar sink[] = "sink";
//                gchar src[] = "src";
//                if (check_links(pipe->alsasrc, src) < 0) g_main_loop_quit(loop);
//                if (check_links(pipe->decodebin, sink) < 0) g_main_loop_quit(loop);
//                if (check_links(pipe->fakesink, sink) < 0) g_main_loop_quit(loop);
//                if (check_links(pipe->audioconvert0, sink) < 0) g_main_loop_quit(loop);
            }
            break;

        case GST_MESSAGE_ELEMENT:
            if ((pipe->pipeline->current_state == GST_STATE_PLAYING) && (terminating == FALSE)) {
                message_handler(msg, pipe->selector);
                if (time_reserve > GUARDING_TIME) {
//                    g_print("\n\t|**************************************************|\n");
//                    g_print("\t|             Exceeded the silent time.            |\n");
//                    g_print("\t|        Now trying to terminate and unref.        |\n");
//                    g_print("\t|**************************************************|\n");
                    g_main_loop_quit(loop);
                }
            }
            break;

        default:
            // We should not reach here
            g_printerr ("Unexpected message received.\n");
            break;

        }
        gst_message_unref (msg);
    }
}














/*static void
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
