#include <gst/gst.h>
#include <glib.h>
#include <gtk/gtk.h>
#include <gdk/gdk.h>
#include <gdk/gdkx.h>
#include <gst/interfaces/xoverlay.h>
#include <stdbool.h>
#include <string.h>

bool fullscreen = false;
bool subState = true;
bool hotlinkSubState = true;
gdouble speedRate = 1 ;





static gboolean bus_call (GstBus     *bus,
                          GstMessage *msg,
                          gpointer data)
{
    GMainLoop *loop = (GMainLoop *) data;

    switch (GST_MESSAGE_TYPE (msg))
    {

    case GST_MESSAGE_EOS:
        g_print ("End of stream\n");
        g_main_loop_quit (loop);
        break;
    case GST_MESSAGE_ERROR:
    {
        gchar  *debug;
        GError *error;

        gst_message_parse_error (msg, &error, &debug);
        g_free (debug);

        g_printerr ("Error: %s\n", error->message);
        g_error_free (error);

        g_main_loop_quit (loop);
        fprintf (stderr, "Couldn't open file %s\n", strerror (errno));
        exit(1);
        break;
    }
    default:
        break;
    }
    return TRUE;
}


static void on_pad_added (GstElement *elementFoo,
                          GstPad     *pad,
                          gpointer    data)
{
    GstPad *sinkpad;
    GstElement *element = (GstElement *) data;

    g_print ("Dynamic pad created, linking element");
    sinkpad = gst_element_get_static_pad (element, "sink");
    g_print ("\t Linking successful\n");

    gst_pad_link (pad, sinkpad);
    gst_object_unref (sinkpad);
}

/*********************
* CALLBACK FUNCTIONS *
**********************/


static void cleanExit(GtkWidget *widget, GstElement* pipeline)
{
    g_print ("Arret de la lecture\n");
    gst_element_set_state (pipeline, GST_STATE_NULL);
    g_print ("Suppression du pipeline\n");
    gst_object_unref (GST_OBJECT (pipeline));
    exit(0);
}


static void stopIt (GtkWidget *widget, GstElement* pipeline)
{
    g_print("Rewinding Video Stream (and resetting speedRate)\n");
    /*
     * gst_element_set_state (pipeline, GST_STATE_READY);
     * causes SIGSEG for no apparent reasons ?
     * so here's a workaround
     */
    gst_element_set_state (pipeline, GST_STATE_PAUSED);
    gst_element_seek_simple (pipeline, GST_FORMAT_TIME, GST_SEEK_FLAG_FLUSH, 0);
    speedRate=1;
}


static void playIt (GtkWidget *widget, GstElement* pipeline)
{
    /*
     * sidenote: I find it completly stupid that you can detect the end of stream in
     * bus_call() with GST_MESSAGE_EOS, but cannot access and reset the state of the pipeline
     * throught the GMainLoop *loop. (If there is a way please, tell me about it)
     * So in order to detect the end of stream, you've got two options:
     *      _ create a second bus
     *      _ pass a struct to bus_call
     *      _ get the stream length and compare it to the current position....
     * I use the third option here, but I'm only skirting the problem to
     * keep the code simple and readable.
     * known issue: if you set the speeed too high (eg:*64), it won't detect the end of
     * the stream correctly, so you'll need to rewind the stream manually by pressing the stop button
     */
    // rewind stream if we're at the end of the stream
    gint64 streamPosition, streamLength;
    GstFormat format = GST_FORMAT_TIME;

    gst_element_query_position (pipeline, &format, &streamPosition);
    gst_element_query_duration (pipeline, &format, &streamLength);
    if (streamPosition==streamLength)
        stopIt(widget,pipeline);


    // setting the stream to the playing state
    g_print("Playing Video\n");
    gst_element_set_state (pipeline, GST_STATE_PLAYING);
}


static void pauseIt (GtkWidget *widget, GstElement* pipeline)
{
   /* pause the pipeline stream */
    g_print("Pausing Video\n");
    gst_element_set_state (pipeline, GST_STATE_PAUSED);
}


static void captureOverlay (GtkWidget *widget, GstElement * videoSink)
{
   /* set video output inside the gtk window */
    gst_x_overlay_set_window_handle(GST_X_OVERLAY(videoSink), GDK_WINDOW_XID(widget->window));
}


static void hotLinkingToggleSub (GtkWidget *widget, GstElement* pipeline)
{
    /*
     * toggle subtitles
     *
     * Could have been done simply by toggling the "silent" property of subtitleOverlay cf toggleSub(...)
     * I just wanted to see how to hotlink elements ^^
     */

    GstElement *subParser = gst_bin_get_by_name(GST_BIN (pipeline), "sub-parser");
    GstElement *subOverlay = gst_bin_get_by_name(GST_BIN (pipeline), "sub-overlay");
    GstElement *videoSink = gst_bin_get_by_name(GST_BIN (pipeline), "video-output");
    GstElement *videoDecoder= gst_bin_get_by_name(GST_BIN (pipeline), "theora-decoder");

    if (hotlinkSubState==true) // subtitles enabled => need to disable them
    {
        gst_element_unlink(subParser, subOverlay);
        gst_element_unlink(videoDecoder, subOverlay);
        gst_element_unlink(subOverlay,videoSink);
        gst_element_link(videoDecoder,videoSink);

        g_print("Subtitles disabled (Hotlinking Method)\n");
        hotlinkSubState=false;
        return;
    }
    else // subtitles disabled => need to enable them
    {
        gst_element_unlink(videoDecoder,videoSink);
        gst_element_link(subParser, subOverlay);
        gst_element_link(videoDecoder, subOverlay);
        gst_element_link(subOverlay,videoSink);
        g_print("Subtitles enabled (Hotlinking Method)\n");
        hotlinkSubState=true;
        return;
    }
}


static void toggleSub (GtkWidget *widget, GstElement* subOverlay)
{
    if (subState==true) // subtitles enabled => need to disable them
    {
        g_object_set (G_OBJECT (subOverlay), "silent", true, NULL);
        g_print("Subtitles disabled\n");
        subState=false;
        return;
    }
    else // subtitles disabled => need to enable them
    {
        g_object_set (G_OBJECT (subOverlay), "silent", false, NULL);
        g_print("Subtitles enabled\n");
        subState=true;
        return;
    }
}


static void toggleFullsreen (GtkWidget *widget, GdkEventButton *event, GtkWidget* windowSPlayer)
{
    /* Toggle fullscreen on the window player by double left clicking the
     * drawing area/movie
     */

    if (event->type==GDK_2BUTTON_PRESS && event->button==1)
    {
        if (fullscreen)
        {
            gtk_window_unfullscreen(GTK_WINDOW(windowSPlayer));
            g_print("Fullscreen disabled\n");
            fullscreen=false;
            return;
        }
        else
        {
            gtk_window_fullscreen(GTK_WINDOW(windowSPlayer));
            g_print("Fullscreen enabled\n");
            fullscreen=true;
            return;
        }
    }
}


static void changeSpeed (GtkWidget *widget, GdkEventKey *event, GstElement* pipeline)
{
    /*
     * Change the playback speed
     * Double it by pressing '+'
     * Half it by pressing '-'s
     *
     *
     * To do this we need to create a new seek_event (describing to the pipeline how to play the movie).
     * It will be the same as the current one, we'll only change the first parameter (rate <=> playback speed)
     * to replicate the current seek_event we need to get the current position of the stream
     * we'll use gst_element_query_position() for that.
     *
     * sidenote: Please tell me, if there is a get_seek_event method because I didn't find it
     */

    GstElement *videoSink = gst_bin_get_by_name(GST_BIN (pipeline), "video-output");
    gint64 streamPosition;
    GstFormat format = GST_FORMAT_TIME;
    GstEvent *seekEvent;
    gst_element_query_position (pipeline, &format, &streamPosition);

    if( strcmp(event->string, "+") == 0 )
        speedRate=speedRate*2;
    else if( strcmp(event->string, "-") == 0 )
        speedRate=speedRate/2;
    else return ;

    seekEvent = gst_event_new_seek (speedRate, GST_FORMAT_TIME, GST_SEEK_FLAG_FLUSH |GST_SEEK_FLAG_ACCURATE,
                                    GST_SEEK_TYPE_SET, streamPosition, GST_SEEK_TYPE_NONE, 0);


    //applying new seekevent=>new speed
    gst_element_send_event (videoSink, seekEvent);

    g_print("playback speed Rate modified to %f \n",speedRate);
}




/********************************************************
*                MAIN                                   *
*********************************************************/

int main (int   argc,
          char *argv[])
{
    /*********************
    *   GSTREAMER INIT   *
    **********************/

    GMainLoop *loop;

    GstElement *pipeline, *sourceVid, *sourceSub, *demuxer, *videoQueue, *videoDecoder, *videoSink, *audioQueue, *audioDecoder, *audioConv, *audioSink, *subOverlay, *subParser;
    GstBus *bus;

    gst_init (&argc, &argv);
    loop = g_main_loop_new (NULL, FALSE);



    switch (argc)
    {
    case 3: // if the user called the program with a third parameter (subtitles)
        /* Initialization of the GstElements used to mangage subtitles */
        subOverlay = gst_element_factory_make("subtitleoverlay", "sub-overlay");
        subParser = gst_element_factory_make("subparse", "sub-parser");
        sourceSub = gst_element_factory_make ("filesrc", "sub-source");
        //verifying initialization
        if ( !subOverlay || !subParser || !sourceSub )
        {
            g_printerr ("One subtitle element could not be created. Exiting.\n");
            return -1;
        }
        subState = 1;
    case 2:
        /* Initialization of the basic video player GstElements */
        pipeline = gst_pipeline_new ("audioVideo-player");
        sourceVid   = gst_element_factory_make ("filesrc", "file-source");
        demuxer  = gst_element_factory_make ("oggdemux", "ogg-demuxer");
        audioQueue = gst_element_factory_make("queue", "audio-queue");
        videoQueue = gst_element_factory_make("queue", "video-queue");
        audioDecoder  = gst_element_factory_make ("vorbisdec", "vorbis-decoder");
        videoDecoder = gst_element_factory_make ("theoradec", "theora-decoder");
        audioConv = gst_element_factory_make ("audioconvert", "audio-converter");
        audioSink = gst_element_factory_make ("autoaudiosink", "audio-output");
        videoSink = gst_element_factory_make ("xvimagesink", "video-output");
        //verifying initialization
        if (!pipeline || !sourceVid || !demuxer || !videoQueue || !audioQueue || !audioDecoder || !videoDecoder || !audioConv || !audioSink || !videoSink)
        {
            g_printerr ("One Basic element could not be created. Exiting.\n");
            return -1;
        }
        break;
    default:
        g_printerr ("Usage: %s <Ogg/Vorbis filename> [<.srt filename>] \n", argv[0]);
        return -1;
    }



    /*********************
    * Initialisation GTK *
    **********************/
    /* Declarations */
    GtkWidget *windowSPlayer; // The one Gtk window
    GtkWidget *mainContainer; // Vertical Container holding video and buttons
    GtkWidget *videoContainer; // Video container
    GtkWidget *videoDrawing; // Video drawing
    GtkWidget *controllerPlayerContainer; //Horizontal Container hodling buttons
    GtkWidget *playButton, *pauseButton, *stopButton, *subButton;
    gtk_init(&argc, &argv);

    /* window */
    windowSPlayer = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_default_size(GTK_WINDOW(windowSPlayer), 800 , 500);
    g_signal_connect( G_OBJECT(windowSPlayer), "destroy", G_CALLBACK(cleanExit), pipeline );
    gtk_widget_add_events (windowSPlayer, GDK_KEY_PRESS); //adding "keyboard key pressed" event for changeSpeed
    g_signal_connect( G_OBJECT(windowSPlayer), "key_press_event", G_CALLBACK(changeSpeed), pipeline );


    /* control Buttons Container*/
    controllerPlayerContainer = gtk_hbox_new (FALSE, 100);
    playButton = gtk_button_new_from_stock (GTK_STOCK_MEDIA_PLAY);
    gtk_box_pack_start (GTK_BOX (controllerPlayerContainer), playButton, TRUE, FALSE, 0);
    g_signal_connect( G_OBJECT(playButton), "clicked", G_CALLBACK(playIt), pipeline );
    pauseButton = gtk_button_new_from_stock (GTK_STOCK_MEDIA_PAUSE);
    gtk_box_pack_start (GTK_BOX (controllerPlayerContainer), pauseButton, TRUE, FALSE, 0);
    g_signal_connect( G_OBJECT(pauseButton), "clicked", G_CALLBACK(pauseIt), pipeline );
    stopButton = gtk_button_new_from_stock (GTK_STOCK_MEDIA_STOP);
    gtk_box_pack_start (GTK_BOX (controllerPlayerContainer), stopButton, TRUE, FALSE, 0);
    g_signal_connect( G_OBJECT(stopButton), "clicked", G_CALLBACK(stopIt), pipeline );
    if (argc==3)
    {
        subButton = gtk_button_new_with_label("Subtitles");
        gtk_box_pack_start (GTK_BOX (controllerPlayerContainer), subButton, TRUE, FALSE, 0);
        g_signal_connect ( G_OBJECT(subButton), "clicked", G_CALLBACK(toggleSub), subOverlay );
        subButton = gtk_button_new_with_label("Subtitles (Unstable Hotlinking Method)");
        gtk_box_pack_start (GTK_BOX (controllerPlayerContainer), subButton, TRUE, FALSE, 0);
        g_signal_connect ( G_OBJECT(subButton), "clicked", G_CALLBACK(hotLinkingToggleSub), pipeline );
    }

    /* video Container */
    videoContainer = gtk_hbox_new (FALSE, 100);
    videoDrawing = gtk_drawing_area_new ();
    gtk_box_pack_start (GTK_BOX (videoContainer), videoDrawing, TRUE, TRUE, 0);
    g_signal_connect ( G_OBJECT(videoDrawing), "realize", G_CALLBACK(captureOverlay), videoSink );
    gtk_widget_add_events (videoDrawing, GDK_BUTTON_PRESS_MASK); //adding "mouse click" event for fullscreen
    g_signal_connect ( G_OBJECT(videoDrawing), "button_press_event", G_CALLBACK(toggleFullsreen), windowSPlayer );

    /* main Container*/
    mainContainer = gtk_vbox_new (FALSE, 0);
    gtk_box_pack_start (GTK_BOX (mainContainer), videoContainer, TRUE, TRUE, 0);
    gtk_box_pack_start (GTK_BOX (mainContainer), controllerPlayerContainer, FALSE, FALSE, 0);

    /* adding main Container to window and print the result */
    gtk_container_add (GTK_CONTAINER (windowSPlayer), mainContainer);

    /* show me your magic gtk! */
    gtk_widget_show_all(windowSPlayer);



    /*********************
    *GSTREAMER MANAGEMENT*
    *********************/

    /* Setting up the pipeline */
    /* Use 1st parameter given by the user as a video file */
    g_object_set (G_OBJECT (sourceVid), "location", argv[1], NULL);
    /* Use 2nd parameter given by the user (if any) as a subtitle file*/
    if (argc==3)
        g_object_set (G_OBJECT (sourceSub), "location", argv[2], NULL);

    /* Let's handle pipeline's GST_MESSAGES
     * usefull to detect errors or end of stream among other things
     */
    bus = gst_pipeline_get_bus (GST_PIPELINE (pipeline));
    gst_bus_add_watch (bus, bus_call, loop);
    gst_object_unref (bus);


    /* Adding GstElements to the pipeline */
    gst_bin_add_many (GST_BIN (pipeline),
                      sourceVid, demuxer, videoQueue, audioQueue, audioDecoder, videoDecoder, audioConv, audioSink, videoSink, NULL);
    if (argc==3) // don't add subtitles management Gst Elements if no subtitle is given to the program
        gst_bin_add_many(GST_BIN (pipeline), sourceSub, subOverlay, subParser, NULL);


    /* Linking GstElements to each other */
    gst_element_link (sourceVid, demuxer); //queue Demuxer
    gst_element_link_many (audioQueue, audioDecoder, audioConv, audioSink, NULL); //queue Audio
    if (argc==3) // don't link subtitles management elements if there is none
    {
        gst_element_link_many (videoQueue, videoDecoder, subOverlay, videoSink, NULL); //queue Video
        gst_element_link_many (sourceSub, subParser, subOverlay, NULL);	//subtitle Sequence
    }
    else
        gst_element_link_many (videoQueue, videoDecoder, videoSink, NULL); //queue Video

    g_signal_connect (demuxer, "pad-added", G_CALLBACK (on_pad_added), audioQueue);
    g_signal_connect (demuxer, "pad-added", G_CALLBACK (on_pad_added), videoQueue);


    /* Setting pipeline to the PLAYING state/start playing the file */
    g_print ("Lecture de : %s\n", argv[1]);
    gst_element_set_state (pipeline, GST_STATE_PLAYING);
    g_print ("En cours...\n");



    /*********************
    *  GTK LOOP !        *
    *  DO YOUR MAGIC !   *
    *********************/
    gtk_main();



    return 0;
}

