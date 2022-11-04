#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>

#include <sys/ioctl.h>
#include <sys/mman.h>

#include "framebuffer.h"

struct type_name {
	unsigned int type;
	const char *name;
};

static const struct type_name connector_type_names[] = {
	{ DRM_MODE_CONNECTOR_Unknown, "unknown" },
	{ DRM_MODE_CONNECTOR_VGA, "VGA" },
	{ DRM_MODE_CONNECTOR_DVII, "DVI-I" },
	{ DRM_MODE_CONNECTOR_DVID, "DVI-D" },
	{ DRM_MODE_CONNECTOR_DVIA, "DVI-A" },
	{ DRM_MODE_CONNECTOR_Composite, "composite" },
	{ DRM_MODE_CONNECTOR_SVIDEO, "s-video" },
	{ DRM_MODE_CONNECTOR_LVDS, "LVDS" },
	{ DRM_MODE_CONNECTOR_Component, "component" },
	{ DRM_MODE_CONNECTOR_9PinDIN, "9-pin DIN" },
	{ DRM_MODE_CONNECTOR_DisplayPort, "DP" },
	{ DRM_MODE_CONNECTOR_HDMIA, "HDMI-A" },
	{ DRM_MODE_CONNECTOR_HDMIB, "HDMI-B" },
	{ DRM_MODE_CONNECTOR_TV, "TV" },
	{ DRM_MODE_CONNECTOR_eDP, "eDP" },
	{ DRM_MODE_CONNECTOR_VIRTUAL, "Virtual" },
	{ DRM_MODE_CONNECTOR_DSI, "DSI" },
	{ DRM_MODE_CONNECTOR_DPI, "DPI" },
};

const char *connector_type_name(unsigned int type)
{
	if (type < ARRAY_SIZE(connector_type_names) && type >= 0) {
		return connector_type_names[type].name;
	}

	return "INVALID";
}

void release_framebuffer(struct framebuffer *fb)
{
	if (fb->fd) {
		/* Try to become master again, else we can't set CRTC. Then the current master needs to reset everything. */
		drmSetMaster(fb->fd);
		if (fb->crtc) {
			/* Set back to orignal frame buffer */
			drmModeSetCrtc(fb->fd, fb->crtc->crtc_id, fb->crtc->buffer_id, 0, 0, &fb->connector->connector_id, 1, fb->resolution);
			drmModeFreeCrtc(fb->crtc);
		}
		if (fb->buffer_id)
			drmModeFreeFB(drmModeGetFB(fb->fd, fb->buffer_id));
		/* This will also release resolution */
		if (fb->connector) {
			drmModeFreeConnector(fb->connector);
			fb->resolution = 0;
		}
		if (fb->dumb_framebuffer.handle)
			ioctl(fb->fd, DRM_IOCTL_MODE_DESTROY_DUMB, fb->dumb_framebuffer);
		close(fb->fd);
	}

}

int get_framebuffer(int select_mode, struct framebuffer *fb)
{
	int err;
	drmModeEncoderPtr encoder = 0;

	if (!G_conn)
	{
		fprintf(stderr,"connector not opened\n");
		return -EINVAL;
	}

	// Get the preferred resolution
	drmModeModeInfoPtr resolution = 0;
	if(select_mode>=0)
	{
		if(select_mode<G_conn->count_modes)
			resolution = &G_conn->modes[select_mode];
	}else{
		for (int i = 0; i < G_conn->count_modes; i++) {
				drmModeModeInfoPtr res = 0;
				res = &G_conn->modes[i];
				if (res->type & DRM_MODE_TYPE_PREFERRED)
						resolution = res;
		}
	}

	if (!resolution)
	{
		fprintf(stderr,"Could not find preferred resolution\n");
		err = -EINVAL;
		goto cleanup;
	}

	fb->dumb_framebuffer.height = resolution->vdisplay;
	fb->dumb_framebuffer.width = resolution->hdisplay;
	fb->dumb_framebuffer.bpp = 32;

	err = ioctl(G_drm_dev, DRM_IOCTL_MODE_CREATE_DUMB, &fb->dumb_framebuffer);
	if (err) {
		printf("Could not create dumb framebuffer (err=%d)\n", err);
		goto cleanup;
	}

	err = drmModeAddFB(G_drm_dev, resolution->hdisplay, resolution->vdisplay, 24, 32,
			fb->dumb_framebuffer.pitch, fb->dumb_framebuffer.handle, &fb->buffer_id);
	if (err) {
		printf("Could not add framebuffer to drm (err=%d)\n", err);
		goto cleanup;
	}

	encoder = drmModeGetEncoder(G_drm_dev, G_conn->encoder_id);
	if (!encoder) {
		printf("Could not get encoder\n");
		err = -EINVAL;
		goto cleanup;
	}

	/* Get the crtc settings */
	fb->crtc = drmModeGetCrtc(G_drm_dev, encoder->crtc_id);

	struct drm_mode_map_dumb mreq;

	memset(&mreq, 0, sizeof(mreq));
	mreq.handle = fb->dumb_framebuffer.handle;

	err = drmIoctl(G_drm_dev, DRM_IOCTL_MODE_MAP_DUMB, &mreq);
	if (err) {
		printf("Mode map dumb framebuffer failed (err=%d)\n", err);
		goto cleanup;
	}

	fb->data = mmap(0,fb->dumb_framebuffer.size,PROT_READ|PROT_WRITE,MAP_SHARED,G_drm_dev,mreq.offset);
	if (fb->data == MAP_FAILED) {
		err = errno;
		printf("Mode map failed (err=%d)\n", err);
		goto cleanup;
	}

	/* Make sure we are not master anymore so that other processes can add new framebuffers as well */
	drmDropMaster(G_drm_dev);

	fb->fd = G_drm_dev;
	fb->connector = G_conn;
	fb->resolution = resolution;

cleanup:
	/* We don't need the encoder and connector anymore so let's free them */
	if (encoder)
		drmModeFreeEncoder(encoder);

	if (err)
		release_framebuffer(fb);

	return err;
}

