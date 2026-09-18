#ifndef VOICEACM_H
#define VOICEACM_H

void InitVoiceAcm(void);
/* Nonzero while UAC install worker is pending. */
int VoiceAcmIsRestarting(void);

#endif
