#include <stdio.h>
#include <math.h>
#include <signal.h>
#include <unistd.h>
#include <time.h>

#define GLIB_DISABLE_DEPRECATION_WARNINGS

#include <gst/gst.h>

#define FILESINK_LOC   "/home/root/WordRemove/"
#define SMR_dB          -40
#define GUARDING_TIME   0.6 * 1000000000

gboolean counting = FALSE, first_voice = TRUE;
GstClockTime flag_time, time_reserve = 0;
gboolean awake = FALSE;

// src0 is for only voice, src1 is for dumping
GstPad *src0_pad = NULL;
GstPad *src1_pad = NULL;

GMainLoop *loop;

// Pipe structure defined here
typedef struct _PipeStruct
{
    GstElement  *pipeline;
    GstElement  *alsasrc, *decodebin, *audioresample,
                *level, *selector,
                *audioconvert0, *flacenc0, *filesink0, *fakesink;
} PipeStruct;


static gint
newElements (PipeStruct *pipe)
{
    pipe->pipeline = gst_pipeline_new ("pipeline");
    g_assert (pipe->pipeline);
    pipe->alsasrc = gst_element_factory_make ("alsasrc", "alsasource");
    g_assert (pipe->alsasrc);
    pipe->decodebin = gst_element_factory_make ("decodebin", "decoder");
    g_assert (pipe->decodebin);
    pipe->audioresample = gst_element_factory_make ("audioresample", "resample");
    g_assert (pipe->audioresample);
    pipe->level = gst_element_factory_make ("level", "level");
    g_assert (pipe->level);
    pipe->selector = gst_element_factory_make ("output-selector", "multiplex");
    g_assert (pipe->selector);
    pipe->audioconvert0 = gst_element_factory_make ("audioconvert", "convert0");
    g_assert (pipe->audioconvert0);
    pipe->flacenc0 = gst_element_factory_make ("flacenc", "encoder0");
    g_assert (pipe->flacenc0);
    pipe->filesink0 = gst_element_factory_make ("filesink", "sink0");
    g_assert (pipe->filesink0);
    pipe->fakesink= gst_element_factory_make ("fakesink", "sink1");
    g_assert (pipe->fakesink);

    if (!pipe->alsasrc || !pipe->decodebin || !pipe->audioresample || !pipe->level ||
        !pipe->selector || !pipe->audioconvert0 || !pipe->flacenc0 || !pipe->filesink0 || !pipe->fakesink) {
        g_error ("Elements couldn't be created.\n");
        if (pipe->pipeline)
            gst_object_unref(pipe->pipeline);
        return -1;
    }
    return 0;
}


static gint
setLinks (PipeStruct *pipe) {
    //We link everything we can link. The rest will be linked later (decoder_pad_handler and selector_pad_handler)
    if (!gst_element_link_many (pipe->alsasrc, pipe->decodebin, NULL)) {
        g_error ("Failure at linking alsasrc and decodebin. Check links. Executing\n");
        gst_object_unref(pipe->pipeline);
        return -1;
    }
    if (!gst_element_link_many (pipe->audioresample, pipe->level, pipe->selector, NULL)) {
        g_error ("Failure at linking convert and further until output_selector. Check links. Executing\n");
        gst_object_unref(pipe->pipeline);
        return -1;
    }
    if (!gst_element_link_many (pipe->audioconvert0, pipe->flacenc0, pipe->filesink0, NULL)) {
        g_error ("Failure at linking audioconvert0, flacenc0 and filesink0. Check links. Executing\n");
        gst_object_unref(pipe->pipeline);
        return -1;
    }
    return 0;
}


static void
setProperties (PipeStruct *pipe)
{
    g_object_set (G_OBJECT (pipe->alsasrc), "device", "dsnoop_micwm", NULL);
    //g_object_set (G_OBJECT (pipe->alsasrc), "blocksize", 8192, NULL);

    //g_object_set (G_OBJECT (pipe->decodebin), "max-size-buffers", 8192, NULL);

    g_object_set (G_OBJECT (pipe->selector), "pad-negotiation-mode", 2, NULL);
    g_object_set (G_OBJECT (pipe->selector), "resend-latest", TRUE, NULL);

    //g_object_set (G_OBJECT (pipe->flacenc0), "blocksize", 4096, NULL);

    //g_object_set (G_OBJECT (pipe->filesink0), "buffer-size", 8192, NULL);
    g_object_set (G_OBJECT (pipe->filesink0), "async", FALSE, NULL);

    g_object_set (G_OBJECT (pipe->fakesink), "async", FALSE, NULL);
    g_object_set (G_OBJECT (pipe->fakesink), "dump", FALSE, NULL);
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


static gint
selector_pad_handler (PipeStruct *pipe)
{
    src0_pad = gst_element_get_request_pad (pipe->selector, "src_%u");
    src1_pad = gst_element_get_request_pad (pipe->selector, "src_%u");

    GstPad *sink0_pad = gst_element_get_static_pad (pipe->audioconvert0, "sink");
    GstPad *sink1_pad = gst_element_get_static_pad (pipe->fakesink, "sink");

    if ((gst_pad_link(src0_pad, sink0_pad)) != GST_PAD_LINK_OK) {
        g_error ("Unable to link audioconvert0\n");
        g_object_unref(pipe->pipeline);
        g_object_unref(sink0_pad);
        return -1;
    }
    if ((gst_pad_link(src1_pad, sink1_pad)) != GST_PAD_LINK_OK) {
        g_error ("Unable to link fakesink\n");
        g_object_unref(pipe->pipeline);
        g_object_unref(sink1_pad);
        return -1;
    }

    g_object_unref(sink0_pad);
    g_object_unref(sink1_pad);

    return 0;
}


static void
level_handling (gdouble RMS_dB, GstClockTime endtime, GstElement *selector)
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
            g_object_set (G_OBJECT (selector), "active_pad", src0_pad, NULL);
            g_print("Successfully changed pads in selector!\n");
        }
        time_reserve = 0;
        counting = FALSE;
        first_voice = FALSE;
    }
}


static void
message_handler (GstMessage *message, GstElement *selector)
{
    const GstStructure *message_structure = gst_message_get_structure(message);
    const gchar *name = gst_structure_get_name(message_structure);

    message_structure = gst_message_get_structure (message);
    name = gst_structure_get_name(message_structure);

    //g_print ("\n*******DETECTED '%s' AS MESSAGE*******\n", name);

    if (g_strcmp0 (name, "level") == 0) {

        GstClockTime endtime;
        const GValue *array_val;
        const GValue *value;
        gdouble rms_dB/*, peak_dB, decay_dB*/;
        //gdouble rms;
        GValueArray *rms_arr/*, *peak_arr, *decay_arr*/;

        //Parsing values for statistics
        if (!gst_structure_get_clock_time (message_structure, "endtime", &endtime))
            g_warning ("Could not parse endtime");

        array_val = gst_structure_get_value (message_structure, "rms");
        rms_arr = (GValueArray *) g_value_get_boxed (array_val);

        /*array_val = gst_structure_get_value (message_structure, "peak");
        peak_arr = (GValueArray *) g_value_get_boxed (array_val);

        array_val = gst_structure_get_value (message_structure, "decay");
        decay_arr = (GValueArray *) g_value_get_boxed (array_val);


        g_print ("\n    endtime: %" GST_TIME_FORMAT "\n", GST_TIME_ARGS (endtime));

        value = g_value_array_get_nth (peak_arr, 0);
        peak_dB = g_value_get_double (value);

        value = g_value_array_get_nth (decay_arr, 0);
        decay_dB = g_value_get_double (value);
        g_print ("      RMS: %f dB, peak: %f dB, decay: %f dB\n",
                 rms_dB, peak_dB, decay_dB);*/
        value = g_value_array_get_nth (rms_arr, 0);
        rms_dB = g_value_get_double (value);
        g_print ("      RMS: %f dB\n", rms_dB);

        //converting from dB to normal gives us a value between 0.0 and 1.0

//        rms = pow (10, rms_dB / 20);
//        g_print ("      normalized rms value: %f\n", rms);

        level_handling (rms_dB, endtime, selector);

//        g_print("flag_time: %" GST_TIME_FORMAT "     time_reserve: %" GST_TIME_FORMAT "\n",
//                GST_TIME_ARGS (flag_time), GST_TIME_ARGS (time_reserve));
    }
}


static void
wakeup(int signum) {
    if (signum == 36) {
        g_print("I'm awake!\n");
        awake = TRUE;
    }
    else
        g_print("Received signal %d\n", signum);
}


static void
writeDoneFile(){
    FILE *status = fopen("/home/root/WordRemove/donestatus", "w");
    fprintf(status, "done");
    fclose(status);
}


static void
message_cb (GstBus *bus, GstMessage *msg, PipeStruct *pipe);







//static gchar
//*newFileNameforSink (gint counter) {
//    static gchar filesink [sizeof(FILESINK_LOC) + 8];
//    strcpy (filesink, FILESINK_LOC);
//    do {
//        counter = counter % 10;
//        gchar unit = counter + '0';
//        strcat (filesink, &unit);
//    } while (!(counter < 10));
//    strcat (filesink, ".flac");
//    return filesink;
//}
