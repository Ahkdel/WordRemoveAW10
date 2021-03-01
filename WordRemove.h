#include <stdio.h>
#include <math.h>
#include <signal.h>
#include <unistd.h>
#include <time.h>

#define GLIB_DISABLE_DEPRECATION_WARNINGS
#define GUARDING_TIME   0.6 * 1000000000

#include <gst/gst.h>

//Leveling flags
gboolean counting = FALSE;
gboolean first_voice = TRUE;
gboolean recording = FALSE;
gfloat SMR_dB;
GstClockTime flag_time;
GstClockTime time_reserve = 0;

gchar *pathname = (char*) malloc(1);
gint event_id;
gboolean EOS_flag = FALSE;
gboolean awake = FALSE;

GMainLoop *loop;

// Pipe structure defined here
typedef struct _PipeStruct
{
    GstElement  *pipeline;
    GstElement  *alsasrc, *decodebin, *audioresample,
    *audioconvert0, *level, *queue,
    *flacenc0, *filesink0,
    *fakesink;
} PipeStruct;

static void newElements (PipeStruct *);
static gboolean setLinks (PipeStruct *);
static void setProperties(PipeStruct *);
static void setLocationFilesink (GstElement *);
static void create_filesink (PipeStruct *);
static void decoder_pad_handler (GstElement *, GstPad *, PipeStruct *);
static void drained_callback (GstElement *, PipeStruct *);
static void level_handling (gdouble, GstClockTime, PipeStruct *);
static void message_handler (GstMessage *, PipeStruct *);
static gboolean check_links (GstElement *, const gchar *);
static void writeDoneFile (void);

static void wakeup (int);

//CALLBACKS
static gboolean timeout_cb(PipeStruct *);
static gboolean timeout_sleep_cb (PipeStruct *);
static GstPadProbeReturn eos_event_cb (GstPad *, GstPadProbeInfo *, GstElement *);
static GstPadProbeReturn sink_swap_cb (GstPad *, GstPadProbeInfo *, PipeStruct *);
static void message_cb (GstBus *, GstMessage *, PipeStruct *);



static void
newElements (PipeStruct *pipe)
{
    pipe->pipeline = gst_pipeline_new ("pipeline");
    pipe->alsasrc = gst_element_factory_make ("alsasrc", "alsasource");
    pipe->decodebin = gst_element_factory_make ("decodebin", "decoder");
    pipe->audioresample = gst_element_factory_make ("audioresample", "resample");
    pipe->audioconvert0 = gst_element_factory_make ("audioconvert", "convert0");
    pipe->level = gst_element_factory_make ("level", "level");
    pipe->queue = gst_element_factory_make("queue", "queue");
    pipe->fakesink= gst_element_factory_make ("fakesink", "fakesink");

    pipe->flacenc0 = NULL;
    pipe->filesink0 = NULL;

    g_assert (pipe->pipeline);
    g_assert (pipe->alsasrc);
    g_assert (pipe->decodebin);
    g_assert (pipe->audioresample);
    g_assert (pipe->audioconvert0);
    g_assert (pipe->level);
    g_assert (pipe->queue);
    g_assert (pipe->fakesink);
}

static gboolean
setLinks (PipeStruct *pipe)
{
    //We link everything we can link. The rest will be linked later (decoder_pad_handler and selector_pad_handler)
    if (!gst_element_link_many (pipe->alsasrc, pipe->decodebin, NULL)) {
        g_error ("Failure at linking alsasrc and decodebin. Check links. Executing.");
        gst_object_unref(pipe->pipeline);
        return FALSE;
    }
    if (!gst_element_link_many (pipe->audioresample, pipe->audioconvert0, pipe->level, pipe->queue, pipe->fakesink, NULL)) {
        g_error ("Failure at linking audioresample and further until fakesink. Check links. Executing.");
        gst_object_unref(pipe->pipeline);
        return FALSE;
    }
    return TRUE;
}

static void
setProperties (PipeStruct *pipe)
{
    g_object_set (G_OBJECT (pipe->alsasrc), "device", "dsnoop_micwm", NULL);
    g_object_set (G_OBJECT (pipe->fakesink), "async", FALSE, NULL);
    g_object_set (G_OBJECT (pipe->fakesink), "dump", FALSE, NULL);
    /*
    g_object_set (G_OBJECT (pipe->alsasrc), "blocksize", 8192, NULL);
    g_object_set (G_OBJECT (pipe->queue), "flush-on-eos", TRUE, NULL);
    //g_object_set (G_OBJECT (pipe->decodebin), "max-size-buffers", 8192, NULL);

    //g_object_set (G_OBJECT (pipe->flacenc0), "blocksize", 4096, NULL);

    //g_object_set (G_OBJECT (pipe->filesink0), "buffer-size", 8192, NULL);
    //g_object_set (G_OBJECT (pipe->filesink0), "async", FALSE, NULL);
    //g_object_set (G_OBJECT (pipe->filesink1), "async", FALSE, NULL);*/
}

static void
setLocationFilesink (GstElement *filesink)
{
    g_print("Got into setLocationFilesink.\nOld pathname: '%s'\n", pathname);
    gchar *length = (char*) malloc(3);
    FILE *file = fopen("/home/root/WordRemove/status", "r");
    length = (char*) realloc(length,3);
    fscanf(file, "%s", length);
    pathname = (char*) realloc(pathname,atoi(length)+1);
    fscanf(file, "%s", pathname);
    g_object_set (G_OBJECT (filesink), "location", pathname, NULL);
    g_print("New pathname: '%s'\n", pathname);
    fclose(file);
}

static void
create_filesink (PipeStruct *pipe)
{
    pipe->flacenc0 = gst_element_factory_make ("flacenc", NULL);
    pipe->filesink0 = gst_element_factory_make ("filesink", NULL);
    g_assert(pipe->flacenc0);
    g_assert (pipe->filesink0);
    gst_bin_add_many(GST_BIN(pipe->pipeline), pipe->flacenc0, pipe->filesink0, NULL);

    if (!gst_element_link(pipe->flacenc0, pipe->filesink0)) {
        g_error("Failure to link flacenc and filesink. Check links. Executing.");
        gst_object_unref(pipe->pipeline);
    } g_print("Successfully linked flacenc with filesink\n");
    g_object_set (G_OBJECT (pipe->filesink0), "async", FALSE, NULL);
}

static void
decoder_pad_handler (GstElement *src, GstPad *new_pad, PipeStruct *pipe)
{
    GstPad *sink_pad = gst_element_get_static_pad (pipe->audioresample, "sink");
    GstPadLinkReturn ret;
    GstCaps *new_pad_caps = NULL;
    GstStructure *new_pad_struct = NULL;
    const gchar *new_pad_type = NULL;

    g_print ("Received new pad '%s' from '%s':\n", GST_PAD_NAME (new_pad), GST_ELEMENT_NAME (src));

    // If our converter is already linked, we have nothing to do here
    if (gst_pad_is_linked (sink_pad)) {
        g_print ("We are already linked. Ignoring.\n");
        goto exit;
    }

    // Check the new pad's type
    new_pad_caps = gst_pad_get_current_caps (new_pad);
    new_pad_struct = gst_caps_get_structure (new_pad_caps, 0);
    new_pad_type = gst_structure_get_name (new_pad_struct);
    if (!g_str_has_prefix (new_pad_type, "audio/x-raw")) {
        g_print ("It has type '%s' which is not raw audio. Ignoring.\n", new_pad_type);
        goto exit;
    }

    // Attempt the link
    ret = gst_pad_link (new_pad, sink_pad);
    if (GST_PAD_LINK_FAILED (ret)) {
        g_print ("Type is '%s' but link failed.\n", new_pad_type);
        gst_object_unref(pipe->pipeline);
    } else {
        g_print ("Link succeeded (type '%s').\n", new_pad_type);
    }

exit:
    // Unreference the new pad's caps, if we got them
    if (new_pad_caps != NULL)
        gst_caps_unref (new_pad_caps);

    // Unreference the sink pad
    gst_object_unref (sink_pad);
}

static void
drained_callback (GstElement *decodebin, PipeStruct *pipe)
{
    g_print("...I'm in drained callback...\n");
    GstState state;
    gst_element_get_state(pipe->filesink0, &state, NULL, GST_CLOCK_TIME_NONE);
    g_print("Filesink state: %d\n", state);
    gst_element_get_state(pipe->pipeline, &state, NULL, GST_CLOCK_TIME_NONE);
    g_print("Pipeline state: %d\n", state);
    gst_element_get_state(decodebin, &state, NULL, GST_CLOCK_TIME_NONE);
    g_print("Decodebin state: %d\n", state);
    g_print("End of drained callback\n");
}

//Can we optimize level_handling or at least write it better, please?
static void
level_handling (gdouble RMS_dB, GstClockTime endtime, PipeStruct *pipe)
{
    //Either I'm listening to noise or voice. Mark the start of this silence between the syllabus with 'flag_time'
    if ((RMS_dB < SMR_dB) && (time_reserve == 0) && !counting && !first_voice) {
        flag_time = endtime;
    }

    //Measure how much does last the 'noise'
    if ((RMS_dB < SMR_dB) && (flag_time > 0) && (!first_voice)) {
        time_reserve = time_reserve + (endtime - flag_time);
        flag_time = endtime;
        counting = TRUE;
    }

    //If the 'noise' is short enough, then it's not noise. Remeasure when ANY noise is found again.
    else if ((RMS_dB > SMR_dB) && (time_reserve < GUARDING_TIME)) {
        if (first_voice) {
            gst_pad_add_probe(gst_element_get_static_pad(pipe->queue, "src"),
                              GstPadProbeType(GST_PAD_PROBE_TYPE_BLOCK),
                              GstPadProbeCallback(sink_swap_cb), pipe, NULL);
            g_print("-> Adding blocking probe to swap sinks <-\n");
            g_source_remove(event_id);
        }
        time_reserve = 0;
        counting = FALSE;
        first_voice = FALSE;
    }
}

static void
message_handler (GstMessage *message, PipeStruct *pipe)
{
    const GstStructure *message_structure = gst_message_get_structure(message);
    const gchar *name = gst_structure_get_name(message_structure);

    g_print ("\n*******DETECTED MESSAGE FROM THE ELEMENT '%s'*******\n", name);

    if (g_strcmp0 (name, "level") == 0)
    {
        gdouble rms_dB/*, peak_dB, decay_dB*/;
        //gdouble rms;
        const GValue *array_val;
        const GValue *value;
        GValueArray *rms_arr/*, *peak_arr, *decay_arr*/;
        GstClockTime endtime;

        //Parsing values for statistics
        if (!gst_structure_get_clock_time (message_structure, "endtime", &endtime))
            g_warning ("Could not parse endtime");

        array_val = gst_structure_get_value (message_structure, "rms");
        rms_arr = (GValueArray *) g_value_get_boxed (array_val);

        /*
        array_val = gst_structure_get_value (message_structure, "peak");
        peak_arr = (GValueArray *) g_value_get_boxed (array_val);

        array_val = gst_structure_get_value (message_structure, "decay");
        decay_arr = (GValueArray *) g_value_get_boxed (array_val);

        value = g_value_array_get_nth (peak_arr, 0);
        peak_dB = g_value_get_double (value);

        value = g_value_array_get_nth (decay_arr, 0);
        decay_dB = g_value_get_double (value);
        g_print ("      RMS: %f dB, peak: %f dB, decay: %f dB\n",
                 rms_dB, peak_dB, decay_dB);*/

        value = g_value_array_get_nth (rms_arr, 0);
        rms_dB = g_value_get_double (value);
        g_print ("\tRMS: %f dB\n", rms_dB);

        //Converting from dB to normal gives us a value between 0.0 and 1.0
        //rms = pow (10, rms_dB / 20);
        //g_print ("normalized rms value: %f\n", rms);

        level_handling (rms_dB, endtime, pipe);
        g_print("endtime:   %" GST_TIME_FORMAT "\n", GST_TIME_ARGS (endtime));
        g_print("flag_time: %" GST_TIME_FORMAT "  ||  time_reserve: %" GST_TIME_FORMAT "\n",
                GST_TIME_ARGS (flag_time), GST_TIME_ARGS (time_reserve));
    }
}

static gboolean
check_links (GstElement *element, const gchar *pad)
{
    GstPad *check_pad = gst_element_get_static_pad(element, pad);
    if (!check_pad){
        g_print("Couldn't retrieve the pad of %s\n", gst_pad_get_name(check_pad));
        g_object_unref(check_pad);
        return FALSE;
    }
    if (!gst_pad_is_linked(check_pad)){
        g_print("Pad (%s) is not linked\n", gst_pad_get_name(check_pad));
        g_object_unref(check_pad);
        return FALSE;
    }
    g_object_unref(check_pad);
    return TRUE;
}

static void
writeDoneFile() {
    FILE *status = fopen("/home/root/WordRemove/donestatus", "w");
    fprintf(status, "done");
    fclose(status);
}

//||--------------------------------------------------------||//
//||                    OS SIGNAL HANDLER                   ||//
//||--------------------------------------------------------||//

static void
wakeup(int signum)
{
    if (signum == 36) {
        g_print("I'm awake!\n");
        awake = TRUE;
    } else
        g_print("Received signal %d\n", signum);
}

//||--------------------------------------------------------||//
//||                       CALLBACKS                        ||//
//||--------------------------------------------------------||//

static gboolean
timeout_cb (PipeStruct *pipe)
{
    g_print("\n[DISABLED] Timeout_cb (here we delete '%s' file)\n", pathname);
    //remove(pathname);
    time_reserve = GUARDING_TIME + 1;
    gst_pad_add_probe(gst_element_get_static_pad(pipe->queue, "src"),
                      GstPadProbeType(GST_PAD_PROBE_TYPE_BLOCK),
                      GstPadProbeCallback(sink_swap_cb), pipe, NULL);
    return FALSE;
}

static gboolean
timeout_sleep_cb (PipeStruct *pipe)
{
    if (awake == FALSE)
        return TRUE;

    g_print("\nTimeout_sleep_cb\n");
    if (!pipe->filesink0)
        create_filesink(pipe);

    /*GstState state;
    gst_element_get_state (pipe->flacenc0, &state, NULL, GST_CLOCK_TIME_NONE);
    g_print("Flacenc state: %d  ||  ", state);
    gst_element_get_state(pipe->filesink0, &state, NULL, GST_CLOCK_TIME_NONE);
    g_print("Filesink state: %d\n", state);*/
    setLocationFilesink(pipe->filesink0);

    recording = TRUE;
    event_id = g_timeout_add_seconds(5, GSourceFunc(timeout_cb), pipe);
    return FALSE;
}

static GstPadProbeReturn
eos_event_cb (GstPad *sinkpad, GstPadProbeInfo *info, GstElement *elem)
{
    g_print("  `-> Got into eos_event_cb. Removing probe.\n");
    gst_pad_remove_probe(sinkpad, GST_PAD_PROBE_INFO_ID(info));

    g_print("  `-> Setting element %s's state to NULL\n", gst_element_get_name(elem));
    GstStateChangeReturn returnstate = gst_element_set_state(elem, GST_STATE_NULL);
    g_print("  `-> returnstate[%s] in eos_event_cb = %d\n", gst_element_get_name(elem), returnstate);
    if (returnstate == GST_STATE_CHANGE_FAILURE) {
        g_printerr ("Unable to set pipeline to PLAYING. Executing.\n");
        g_main_loop_quit(loop);
    }
    g_print("  `-> Successfully changed element's state to GST_STATE_NULL\n\n");
    EOS_flag = TRUE;
    return GST_PAD_PROBE_DROP;
}

static GstPadProbeReturn
sink_swap_cb (GstPad *srcpad, GstPadProbeInfo *info, PipeStruct *pipe)
{
    g_print("\n### I'm in the callback! Let's swap the sinks! ###\n");
    gst_pad_remove_probe(srcpad, GST_PAD_PROBE_INFO_ID(info));
    EOS_flag = FALSE;

    if (pipe->fakesink) {
        g_print("[1/7]-> Checked fakesink pads and they are linked\n");
        gst_element_unlink(pipe->queue, pipe->fakesink);
        g_print("[2/7]-> Successfully unlinked queue and fakesink\n");

        gst_pad_add_probe(gst_element_get_static_pad(pipe->fakesink, "sink"),
                          GstPadProbeType(GST_PAD_PROBE_TYPE_EVENT_DOWNSTREAM),
                          GstPadProbeCallback(eos_event_cb), pipe->fakesink, NULL);
        gst_pad_send_event(gst_element_get_static_pad(pipe->fakesink, "sink"), gst_event_new_eos());
        while (!EOS_flag);
        g_print("[3/7]-> Sent and handled the EOS event to fakesink to empty all its data\n");

        gst_bin_remove(GST_BIN(pipe->pipeline), pipe->fakesink);
        pipe->fakesink = NULL;
        g_print("[4/7]-> Set fakesink to NULL and removed it from the bin\n");

        if (!gst_element_link(pipe->queue, pipe->flacenc0)) {
            g_error("Unable to link queue with encoder");
            gst_object_unref(srcpad);
            gst_object_unref(pipe->pipeline);
            return GST_PAD_PROBE_REMOVE;
        } g_print("[5/7]-> Successfully linked queue and flacenc\n");

        gst_element_sync_state_with_parent(pipe->flacenc0);
        g_print("[6/7]-> Synced flacenc's state with its parent\n");
        gst_element_sync_state_with_parent(pipe->filesink0);
        g_print("[7/7]-> Synced filesink's state with its parent\n");
    }

    else {
        g_print("[1/11]-> Checked fakesink pads and they are NOT linked\n");
        gst_element_unlink(pipe->queue, pipe->flacenc0);
        g_print("[2/11]-> Successfully unlinked queue with flacenc\n");
        if (!check_links(pipe->flacenc0, "sink"))
            g_print("[3/11]-> Yes, we are sure that nothing is linked with flacenc\n");

        gst_pad_add_probe(gst_element_get_static_pad(pipe->filesink0, "sink"),
                          GstPadProbeType(GST_PAD_PROBE_TYPE_EVENT_DOWNSTREAM),
                          GstPadProbeCallback(eos_event_cb), pipe->filesink0, NULL);
        gst_pad_send_event(gst_element_get_static_pad(pipe->flacenc0, "sink"), gst_event_new_eos());
        while (!EOS_flag); //We wait until EOS is handled
        g_print("[4/11]-> Sent and handled the EOS event for filesink\n");

        gst_element_unlink(pipe->flacenc0, pipe->filesink0);
        g_print("[5/11]-> Unliked flacenc and filesink\n");
        gst_element_set_state(pipe->flacenc0, GST_STATE_NULL);
        g_print("[6/11]-> Set flacenc's state to NULL\n");
        gst_bin_remove_many(GST_BIN(pipe->pipeline), pipe->flacenc0, pipe->filesink0, NULL);
        g_print("[7/11]-> Removed flacenc and filesink from the bin\n");

        pipe->flacenc0 = NULL;
        pipe->filesink0 = NULL;
        g_print("[8/11]-> Set flacenc and filesink to NULL\n");

        pipe->fakesink = gst_element_factory_make("fakesink", NULL);
        g_assert(pipe->fakesink);
        g_object_set(G_OBJECT(pipe->fakesink), "async", FALSE, NULL);
        gst_bin_add(GST_BIN(pipe->pipeline), pipe->fakesink);
        g_print("[9/11]-> Success: Created fakesink and added it to bin\n");

        if (!gst_element_link(pipe->queue, pipe->fakesink)) {
            g_error("Unable to link queue and fakesink");
            gst_object_unref(srcpad);
            gst_object_unref(pipe->pipeline);
            return GST_PAD_PROBE_REMOVE;
        } g_print("[10/11]-> Successfully linked queue with fakesink\n");
        gst_element_sync_state_with_parent(pipe->fakesink);
        g_print("[11/11]-> Synced fakesink's state with its parent\n");
    }
    return GST_PAD_PROBE_DROP;
}













