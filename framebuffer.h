#ifndef _FRAMEBUFFER_H
#define _FRAMEBUFFER_H

#include <stdint.h>
#include <libdrm/drm.h>
#include <libdrm/drm_mode.h>
#include <xf86drm.h>
#include <xf86drmMode.h>
#include "globals.h"


#define ARRAY_SIZE(array) (sizeof(array) / sizeof(array[0]))

struct framebuffer {
	int fd;
	uint32_t buffer_id;
	uint16_t res_x;
	uint16_t res_y;
	uint8_t *data;
	uint32_t size;
	struct drm_mode_create_dumb dumb_framebuffer;
	drmModeModeInfoPtr resolution;
};

void release_framebuffer(struct framebuffer *fb);
int get_framebuffer(drmModeModeInfoPtr mode, struct framebuffer *fb);

const char *connector_type_name(unsigned int type);

#endif
