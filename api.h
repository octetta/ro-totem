#ifndef SKRED_API_H
#define SKRED_API_H

#ifdef __cplusplus
extern "C" {
#endif

// Initialize the audio engine, state, and networking
int skred_start(unsigned int req_audio_frames, unsigned int voices, int port);

// Send an ASCII control protocol message to the engine
int skred_command(char* cmd);

// Safely tear down resources
void skred_stop(void);

// list of included features
char *skred_features(void);

// did skode have anything to say?
char *skred_log(void);

// enable / disable logging
void skred_logger(int f);

// clumsy enumeration
int skred_devices(int isCapture);
int skred_device_idx(int isCapture, int idx);
char *skred_device_str(int isCapture, int idx);
int skred_enumerate_devices(int isCapture);
void skred_set_audio_device(int playback_idx, int capture_idx);

#ifdef __cplusplus
}
#endif

#endif // SKRED_API_H