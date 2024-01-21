#define _GNU_SOURCE
#include <fcntl.h>
#include <errno.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <signal.h>
#include <linux/videodev2.h>
#include <time.h>

#define VIDEO_DEVICE "/dev/video0"
#define TIMEOUT 2 // Adjust timeout value as needed

// Global variables to store PIDs of connected and disconnected scripts
pid_t connectedScriptPID = -1;
pid_t disconnectedScriptPID = -1;

void logDVTimings(struct v4l2_dv_timings *dv_timings) {
    printf("DV Timings:\n");
    printf("  Type: %u\n", dv_timings->type);
    printf("  BT.656: %u\n", dv_timings->bt);
    printf("  Width: %u\n", dv_timings->bt.width);
    printf("  Height: %u\n", dv_timings->bt.height);
    printf("  Pixelclock: %u Hz\n", dv_timings->bt.pixelclock);
    printf("  H. Frontporch: %u\n", dv_timings->bt.hfrontporch);
    printf("  H. Sync: %u\n", dv_timings->bt.hsync);
    printf("  H. Backporch: %u\n", dv_timings->bt.hbackporch);
    printf("  V. Frontporch: %u\n", dv_timings->bt.vfrontporch);
    printf("  V. Sync: %u\n", dv_timings->bt.vsync);
    printf("  V. Backporch: %u\n", dv_timings->bt.vbackporch);
}

int checkHDMIStatus(int fd, struct v4l2_dv_timings *lastDVTimings) {
    struct v4l2_dv_timings dv_timings;
    int result = ioctl(fd, VIDIOC_QUERY_DV_TIMINGS, &dv_timings);

    // Log the result for debugging
    printf("VIDIOC_QUERY_DV_TIMINGS result: %d\n", result);

    // Log DV Timings whenever dv_timings changes
    if (memcmp(&dv_timings, lastDVTimings, sizeof(struct v4l2_dv_timings)) != 0) {
        logDVTimings(&dv_timings);
        memcpy(lastDVTimings, &dv_timings, sizeof(struct v4l2_dv_timings));

        // Return 1 if HDMI is connected, 0 if disconnected
        return (result >= 0) ? 1 : 0;
    }

    // Return -1 when there is no HDMI status change
    printf("checkHDMIStatus: No HDMI status change\n");
    return -1;
}

void executeConnectedCommands() {
    if (disconnectedScriptPID != -1) {
        // Terminate the connected script using its PID
        if (kill(disconnectedScriptPID, SIGTERM) == 0) {
            printf("Terminated disconnected script with PID: %d\n", disconnectedScriptPID);
        } else {
            perror("Error terminating disconnected script");
        }

        // Reset the PID
        disconnectedScriptPID = -1;
    }
    // Get the full path to gst-launch-1.0
    FILE *gstPath = popen("which gst-launch-1.0", "r");
    char path[256];
    fgets(path, sizeof(path), gstPath);
    pclose(gstPath);

    // Run v4l2-ctl commands
    system("v4l2-ctl --query-dv-timings && v4l2-ctl --set-dv-bt-timings query");
    // Run gst-launch command for video and audio with the full path
    FILE *fp = popen("gst-launch-1.0 v4l2src ! video/x-raw,framerate=60/1,format=UYVY,colorimetry=bt601 ! v4l2convert ! videoscale ! video/x-raw, width=720, height=576 ! fbdevsink async=false sync=false alsasrc ! audio/x-raw,rate=48000,channels=2 ! pulsesink & echo $!", "r");
    if (fp == NULL) {
        perror("Error running connected script");
        return;
    }

    // Read the PID from the output of the command
    if (fscanf(fp, "%d", &connectedScriptPID) != 1) {
        perror("Error reading PID");
        // Note: No pclose(fp) here to keep the process running
        return;
    }

    // Sleep for a brief moment
    sleep(TIMEOUT);

    printf("Connected script started with PID: %d\n", connectedScriptPID);
}

void executeDisconnectedCommands() {
    if (connectedScriptPID != -1) {
        // Terminate the connected script using its PID
        if (kill(connectedScriptPID, SIGTERM) == 0) {
            printf("Terminated connected script with PID: %d\n", connectedScriptPID);
        } else {
            perror("Error terminating connected script");
        }

        // Reset the PID
        connectedScriptPID = -1;
    }

    // Get the full path to gst-launch-1.0
    FILE *gstPath = popen("which gst-launch-1.0", "r");
    char path[256];
    fgets(path, sizeof(path), gstPath);
    pclose(gstPath);

    // Run gst-launch command for disconnection with the full path
    FILE *fp = popen("gst-launch-1.0 videotestsrc pattern=snow ! video/x-raw,width=720,height=576 ! textoverlay text=\"Please Stand By\" valignment=center halignment=center font-desc=\"VCR OSD Mono, 32\" ! fbdevsink & echo $!", "r");
    if (fp == NULL) {
        perror("Error running disconnected script");
        return;
    }

    // Read the PID from the output of the command
    if (fscanf(fp, "%d", &disconnectedScriptPID) != 1) {
        perror("Error reading PID");
        // Note: No pclose(fp) here to keep the process running
        return;
    }

    // Sleep for a brief moment
    sleep(TIMEOUT);

    printf("Disconnected script started with PID: %d\n", disconnectedScriptPID);
}


int main(int argc, char **argv) {
    int fd = open(VIDEO_DEVICE, O_RDWR);
    if (fd == -1) {
        perror("Error opening video device");
        return 1;
    }

    // Check HDMI status on script startup
    struct v4l2_dv_timings startupDVTimings;
    int startupStatus = checkHDMIStatus(fd, &startupDVTimings);
    if (startupStatus == 0) {
        // HDMI disconnected
        printf("HDMI cable already disconnected on startup.\n");
        executeDisconnectedCommands();
    } else if (startupStatus == 1) {
        // HDMI connected
        printf("HDMI cable already connected on startup.\n");
        executeConnectedCommands();
    }

    // Subscribe to V4L2_EVENT_SOURCE_CHANGE events
    struct v4l2_event_subscription sub;
    memset(&sub, 0, sizeof(sub));
    sub.type = V4L2_EVENT_SOURCE_CHANGE;

    if (ioctl(fd, VIDIOC_SUBSCRIBE_EVENT, &sub) == -1) {
        perror("Error subscribing to V4L2_EVENT_SOURCE_CHANGE events");
        close(fd);
        return 1;
    }

    time_t lastEventTime = 0;
    int lastResult = startupStatus; // Initialize with the startup status
    struct v4l2_dv_timings lastDVTimings = startupDVTimings;

    while (1) {
        struct v4l2_event event;
        if (ioctl(fd, VIDIOC_DQEVENT, &event) == -1) {
            if (errno == EAGAIN || errno == EINTR) {
                // No event or interrupted, continue waiting
                continue;
            }
            perror("Error dequeuing event");
            break;
        }

        if (event.type == V4L2_EVENT_SOURCE_CHANGE) {
            // Handle source change event
            time_t currentTime = time(NULL);
            printf("HDMI source change detected! Cable ");

            if (currentTime - lastEventTime >= TIMEOUT) {
                int statusChange = checkHDMIStatus(fd, &lastDVTimings);
                if (statusChange == -1) {
                    // No HDMI status change
                    printf("ignored due to timeout.\n");
                } else if (statusChange == 0) {
                    // HDMI disconnected
                    printf("disconnected.\n");
                    executeDisconnectedCommands();
                } else {
                    // HDMI connected
                    printf("connected.\n");
                    executeConnectedCommands();
                }

                lastEventTime = currentTime;
            } else {
                printf("ignored due to timeout.\n");
            }
        }
    }

    close(fd);
    return 0;
}
