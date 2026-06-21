#ifndef SKRED_API_H
#define SKRED_API_H

#include <stdint.h>
#include "skred-version.h"

#ifdef __cplusplus
extern "C" {
#endif

// Initialize the audio engine, state, and networking
int skred_start(unsigned int req_audio_frames, unsigned int voices, int port);

// Send an ASCII control protocol message to the engine
int skred_command(char* cmd);

// Safely tear down resources
void skred_stop(void);

// RECORD feature: writes master plus four stereo stems to a 10-channel WAV.
int skred_record_start(const char *filename, double max_seconds);
int skred_record_stop(void);
int skred_record_state(void);
uint64_t skred_record_frames_written(void);
uint64_t skred_record_dropped_frames(void);

// SCOPE feature: publishes the 10-channel render bus through shared memory.
int skred_scope_start(const char *name, uint32_t channel_mask,
                      double buffer_seconds);
int skred_scope_stop(void);

// list of included features
char *skred_features(void);

// Version from the repository VERSION file used at build time.
const char *skred_version(void);
int skred_version_major(void);
int skred_version_minor(void);
int skred_version_patch(void);

// did skode have anything to say?
char *skred_log(void);

// enable / disable logging
void skred_logger(int f);

// Audio device management. Selection values are list slots from the latest
// refresh; -1 selects the default device and -2 disables capture.
int skred_audio_refresh(void);
int skred_audio_select(int is_capture, int selection);
int skred_audio_reconnect(void);
int skred_audio_disconnect(void);
int skred_audio_running(void);
char *skred_audio_status(void);
int skred_audio_command(const char *line);
char *skred_audio_message(void);

// Compatibility enumeration API.
int skred_devices(int isCapture);
int skred_device_idx(int isCapture, int idx);
char *skred_device_str(int isCapture, int idx);
int skred_enumerate_devices(int isCapture);
void skred_set_audio_device(int playback_idx, int capture_idx);

#ifdef __cplusplus
}
#endif

#endif // SKRED_API_H
