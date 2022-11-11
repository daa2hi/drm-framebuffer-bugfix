#pragma once

// global variables
#include <libdrm/drm.h>
#include <stdint.h>

extern int G_drm_dev;
extern drmModeResPtr G_drmres;
extern drmModeConnectorPtr G_conn;
extern drmModeEncoderPtr G_enc;
extern drmModeCrtcPtr G_crtc;
extern uint32_t G_crtcId;
extern drmModeModeInfoPtr G_mode;

