#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <gst/gst.h>

static gboolean
ReadyFile (char *filename) {
    FILE *file = fopen("/home/root/WordRemove/status", "r");
    gchar buffer[9], length[3];
    fscanf(file, "%s", buffer);
    g_print ("File buffer: %s\n", buffer);
    if (g_strcmp0(buffer, "ready") == 0) {
        fscanf(file, "%s", length);
        gchar pathname[atoi(length)];
        fscanf(file, "%s", pathname);
        fclose(file);
        return TRUE;
    }
    else if (g_strcmp0(buffer, "error") == 0) {
        g_error ("Error with the file of status.\n");
        fclose(file);
        return FALSE;
    }
    else {
        g_print("File status: not ready\n");
        fclose(file);
        return FALSE;
    }
}
