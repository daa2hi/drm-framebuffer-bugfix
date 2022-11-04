/*
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */


#include <errno.h>
#include <fcntl.h>
#include <getopt.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <unistd.h>


#include "framebuffer.h"

static int verbose = 0;


static void usage(void);
static drmModeConnectorPtr get_connector(const char *con_name);
static int list_resources();
static int get_resolution();
static int fill_framebuffer_from_stdin(struct framebuffer *fb);


// global vars as declared in globals.h
int G_drm_dev = -1;
drmModeResPtr G_drmres = 0;
drmModeConnectorPtr G_conn = 0;

int main(int argc, char** argv)
{
	char *dri_device = 0;
	char *connector = 0;
	int c;
	int list = 0;
	int resolution = 0;
	int ret;
	int mode_select=-1;

	opterr = 0;
	while ((c = getopt (argc, argv, "d:c:lrhvm:")) != -1) {
		switch (c)
		{
		case 'd':
			dri_device = optarg;
			break;
		case 'c':
			connector = optarg;
			break;
		case 'l':
			list = 1;
			break;
		case 'r':
			resolution = 1;
			break;
		case 'm':
			mode_select = strtol(optarg,0,0);
			break;
		case 'h':
			usage();
			return 1;
		case 'v':
			verbose = 1;
			break;
		default:
			break;
		}
	}

	if (dri_device == 0) {
		printf("Please set a device\n");
		return 3;
	}

	G_drm_dev = open(dri_device, O_RDWR);
	if(G_drm_dev<0)
	{
		fprintf(stderr,"Cannot open device file '%s'\n",dri_device);
		ret=1;goto leave;

	}

	// Get the resources of the DRM device (connectors, encoders, etc.)
	G_drmres = drmModeGetResources(G_drm_dev);
	if(!G_drmres)
	{
		fprintf(stderr,"Could not get drm resources\n");
		ret=1;
		goto leave;
	}


	if (list) {
		return list_resources();
	}

	G_conn = get_connector(connector);
	if(!G_conn)
	{
		fprintf(stderr,"Error getting drm connector '%s'\n",connector);
		goto leave;
	}

	if (resolution)
	{
		return get_resolution(dri_device, connector);
	}

	struct framebuffer fb[3];
	fb[0].fd=fb[1].fd=fb[2].fd=-1;
	for(int i=0;i<3;i++)
	{
		if(get_framebuffer(mode_select, fb+i))
		{
			fprintf(stderr,"Error creating framebuffers\n");
			ret = 4;goto leave;
		}
	}
	ret = 1;
	if(!fill_framebuffer_from_stdin(fb+0))
	{
		// successfully shown.
		ret = 0;
	}
leave:
	for(int i=0;i<3;i++)
	{
		if(fb[i].fd>=0)	release_framebuffer(fb+i);
		fb[i].fd=-1;
	}

	if(G_drm_dev>=0)
	{
		if(G_conn)
		{
			drmModeFreeConnector(G_conn);
			G_conn = 0;
		}
		if(G_drmres)
		{
			drmModeFreeResources(G_drmres);
			G_drmres = 0;
		}
		close(G_drm_dev);
	}

	return ret;
}


static void usage(void)
{
	printf ("\ndrm-framebuffer [OPTIONS...]\n\n"
			"Pipe data to a framebuffer\n\n"
			"  -d dri device /dev/dri/cardN\n"
			"  -l list connectors\n"
			"  -c connector to use (HDMI-A-1, LVDS-1)\n"
			"  -r get resolution dri device and connector needs to be set\n"
			"  -v do more verbose printing\n"
			"  -h show this message\n\n");
}

static drmModeConnectorPtr get_connector(const char *con_name)
{
	int i;
	// Search the connector provided as argument
	drmModeConnectorPtr connector = 0;
	for(i=0;i<G_drmres->count_connectors;i++)
	{
		char name[128];

		connector = drmModeGetConnectorCurrent(G_drm_dev,G_drmres->connectors[i]);
		if(!connector)
			continue;

		snprintf(name, sizeof(name), "%s-%u", connector_type_name(connector->connector_type),
				connector->connector_type_id);

		if(strncmp(name, con_name, sizeof(name)) == 0)
				return connector;

		drmModeFreeConnector(connector);
		connector = 0;
	}
	return 0;
}

#define print_verbose(...) if (verbose) printf(__VA_ARGS__)

static int list_resources()
{
	drmModeResPtr res;


	res = drmModeGetResources(G_drm_dev);
	if (!res) {
		printf("Could not get drm resources\n");
		return -EINVAL;
	}

	printf("connectors:\n");
	for (int i = 0; i < res->count_connectors; i++) {
		drmModeConnectorPtr connector = 0;
		drmModeEncoderPtr encoder = 0;

		printf("Number: %d ", res->connectors[i]);
		connector = drmModeGetConnectorCurrent(G_drm_dev, res->connectors[i]);
		if (!connector)
			continue;

		printf("Name: \"%s-%u\" ", connector_type_name(connector->connector_type), connector->connector_type_id);

		printf("Encoder: %d ", connector->encoder_id);

		encoder = drmModeGetEncoder(G_drm_dev, connector->encoder_id);
		if (!encoder)
			{printf("\n");continue;}

		printf("Crtc: %d", encoder->crtc_id);

		drmModeFreeEncoder(encoder);
		drmModeFreeConnector(connector);
		printf("\n");

		for (int j = 0; j < connector->count_modes; j++)
		{
			drmModeModeInfoPtr mode = connector->modes+j;
			printf(
				"	#%d  %u*%u  %uHz  t 0x%02X  f 0x%02X\n" ,
				j ,
				(unsigned int)mode->hdisplay ,
				(unsigned int)mode->vdisplay ,
				(unsigned int)mode->vrefresh ,
				(unsigned int)mode->type ,
				(unsigned int)mode->flags
			);
		}
	}

	printf("\nFramebuffers: ");
	for (int i = 0; i < res->count_fbs; i++) {
		printf("%d ", res->fbs[i]);
	}

	printf("\nCRTCs: ");
	for (int i = 0; i < res->count_crtcs; i++) {
		printf("%d ", res->crtcs[i]);
	}

	printf("\nencoders: ");
	for (int i = 0; i < res->count_encoders; i++) {
		printf("%d ", res->encoders[i]);
	}
	printf("\n");

	drmModeFreeResources(res);

	return 0;
}

static int get_resolution()
{
	int err = 0;
	drmModeResPtr res;

	res = drmModeGetResources(G_drm_dev);
	if (!res)
	{
		printf("Could not get drm resources\n");
		return -EINVAL;
	}

	if (!G_conn)
	{
		printf("Connector not acquired before call.\n");
		return -EINVAL;
	}

	// Get the preferred resolution
	drmModeModeInfoPtr resolution = 0;
	for (int i=0;i<G_conn->count_modes;i++)
	{
		resolution = &G_conn->modes[i];
		if (resolution->type & DRM_MODE_TYPE_PREFERRED)
				break;
	}

	if (!resolution)
	{
		printf("Could not find preferred resolution\n");
		err = -EINVAL;
		goto error;
	}

	printf("%ux%u\n", resolution->hdisplay, resolution->vdisplay);

error:
	drmModeFreeResources(res);

	return err;
}


static int fill_framebuffer_from_stdin(struct framebuffer *fb)
{
	size_t total_read = 0;
	int ret;

	print_verbose("Loading image\n");
	while (total_read < fb->dumb_framebuffer.size)
	{
		size_t sz = read(STDIN_FILENO, &fb->data[total_read], fb->dumb_framebuffer.size - total_read);
		if(sz<=0)		/* stop when getting EOF */
		{
			break;
		}
		total_read += sz;
	}

	/* Make sure we synchronize the display with the buffer. This also works if page flips are enabled */
	ret = drmSetMaster(fb->fd);
	if(ret)
	{
		printf("Could not get master role for DRM.\n");
		return ret;
	}
	drmModeSetCrtc(fb->fd, fb->crtc->crtc_id, 0, 0, 0, NULL, 0, NULL);
	drmModeSetCrtc(fb->fd, fb->crtc->crtc_id, fb->buffer_id, 0, 0, &fb->connector->connector_id, 1, fb->resolution);
	drmDropMaster(fb->fd);

	print_verbose("Sent image to framebuffer\n");

	sigset_t wait_set;
	sigemptyset(&wait_set);
	sigaddset(&wait_set, SIGTERM);
	sigaddset(&wait_set, SIGINT);

	int sig;
	sigprocmask(SIG_BLOCK, &wait_set, NULL );
	sigwait(&wait_set, &sig);

	return 0;
}


