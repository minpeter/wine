/* AI-assisted with Amp. SPDX-License-Identifier: LGPL-2.1-or-later */
#include <dlfcn.h>
#include <stdio.h>
#include <gst/gst.h>

int main(int argc, char **argv)
{
    static const char *factories[] = {
        "audioconvert", "audioresample", "videoconvert", "decodebin", "playbin",
        "avdec_h264", "avdec_aac", "qtdemux", "matroskademux", "oggdemux",
        "vorbisdec", "opusdec", "x264enc", "jpegdec"
    };
    static const char *pipelines[] = {
        "audiotestsrc num-buffers=4 ! audioconvert ! audioresample ! fakesink",
        "videotestsrc num-buffers=4 ! video/x-raw,width=64,height=48,framerate=5/1 ! "
        "x264enc tune=zerolatency ! h264parse ! avdec_h264 ! videoconvert ! fakesink"
    };
    GError *error = NULL;
    GstElement *pipeline;
    GstBus *bus;
    GstMessage *message;
    unsigned int i;
    int failures = 0;

    setvbuf(stdout, NULL, _IOLBF, 0);
    for (i = 1; i < (unsigned int)argc; ++i)
    {
        printf("Loading %s\n", argv[i]);
        void *library = dlopen(argv[i], RTLD_NOW | RTLD_LOCAL);
        if (!library)
        {
            fprintf(stderr, "FAIL dlopen %s: %s\n", argv[i], dlerror());
            ++failures;
        }
        else
        {
            printf("PASS dlopen %s\n", argv[i]);
            dlclose(library);
        }
    }
    if (!gst_init_check(NULL, NULL, &error))
    {
        fprintf(stderr, "FAIL gst_init: %s\n", error->message);
        g_error_free(error);
        return 1;
    }
    for (i = 0; i < sizeof(factories) / sizeof(factories[0]); ++i)
    {
        GstElementFactory *factory = gst_element_factory_find(factories[i]);
        if (!factory)
        {
            fprintf(stderr, "FAIL factory %s\n", factories[i]);
            ++failures;
        }
        else
        {
            printf("PASS factory %s\n", factories[i]);
            gst_object_unref(factory);
        }
    }
    for (i = 0; i < sizeof(pipelines) / sizeof(pipelines[0]); ++i)
    {
        pipeline = gst_parse_launch(pipelines[i], &error);
        if (!pipeline || error)
        {
            fprintf(stderr, "FAIL pipeline construction: %s\n", error ? error->message : "no pipeline");
            if (error) g_error_free(error);
            return 1;
        }
        bus = gst_element_get_bus(pipeline);
        gst_element_set_state(pipeline, GST_STATE_PLAYING);
        message = gst_bus_timed_pop_filtered(bus, 10 * GST_SECOND, GST_MESSAGE_EOS | GST_MESSAGE_ERROR);
        if (!message || GST_MESSAGE_TYPE(message) != GST_MESSAGE_EOS)
        {
            fprintf(stderr, "FAIL pipeline %u did not reach EOS\n", i);
            ++failures;
        }
        else printf("PASS %s pipeline EOS\n", i ? "H264 encode/decode" : "audio");
        if (message) gst_message_unref(message);
        gst_element_set_state(pipeline, GST_STATE_NULL);
        gst_object_unref(bus);
        gst_object_unref(pipeline);
    }
    printf("Runtime probe: %d failures\n", failures);
    return failures ? 1 : 0;
}
