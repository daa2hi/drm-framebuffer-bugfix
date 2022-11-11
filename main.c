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
#include <time.h>
#include <unistd.h>

#include "framebuffer.h"
#include "pattern.h"

static int verbose = 0;


static void usage(void);
static void dummy_page_flip_handler(int fd,unsigned int sequence,unsigned int tv_sec,unsigned int tv_usec,void *user_data);
static drmModeConnectorPtr get_connector(const char *con_name);
static int list_resources();
static drmModeModeInfoPtr find_resolution(int w,int h);
static char get_prev_crtc();
static char restore_prev_crtc();
static char get_master();
static void release_master();
static int fill_framebuffer_from_stdin(struct framebuffer *fb);
static int show_framebuffer(struct framebuffer *fb);
static void wait_break();


// global vars as declared in globals.h
int G_drm_dev = -1;
drmModeResPtr G_drmres = 0;
drmModeConnectorPtr G_conn = 0;
drmModeEncoderPtr G_enc = 0;
drmModeCrtcPtr G_crtc = 0;
uint32_t G_crtcId = 0;
drmModeModeInfoPtr G_mode = 0;
uint16_t G_size_x = 0;
uint16_t G_size_y = 0;

char G_master = 0;

drmModeModeInfo G_prev_mode;
uint32_t G_prev_bufId = 0;

int main(int argc, char** argv)
{
	char *dri_device = 0;
	char *connector = 0;
	int c;
	int list = 0;
	char read_stdin = 0;
	char force_res = 0;
	int ret;
	int choose_width = -1;
	int choose_height = -1;
	char need_wait_flip;
	char waiting;

	opterr = 0;
	while ((c = getopt (argc, argv, "d:c:lhvx:y:if")) != -1) {
		switch (c)
		{
		case 'd':
			dri_device = optarg;
			break;
		case 'i':
			read_stdin = 1;
			break;
		case 'f':
			force_res = 1;
			break;
		case 'c':
			connector = optarg;
			break;
		case 'l':
			list = 1;
			break;
		case 'x':
			choose_width = strtol(optarg,0,0);
			break;
		case 'y':
			choose_height = strtol(optarg,0,0);
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

	if (dri_device==0)
	{
		dri_device = "/dev/dri/card0";
		printf("no device selected with -d . Using default '%s'\n",dri_device);
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

	if (connector==0)
	{
		printf("No connector selcted. Use -l to see all connectors of a device.\n");
		return 1;
	}

	G_conn = get_connector(connector);
	if(!G_conn)
	{
		fprintf(stderr,"Error getting drm connector '%s'\n",connector);
		goto leave;
	}
	G_enc = drmModeGetEncoder(G_drm_dev,G_conn->encoder_id);
	if(!G_enc)
	{
		fprintf(stderr,"Error getting drm encoder for connector\n");
		goto leave;
	}

	G_crtc = drmModeGetCrtc(G_drm_dev,G_enc->crtc_id);
	G_crtcId = G_enc->crtc_id;
	printf("Crtc: %u\n",(unsigned int)(G_crtcId));

	G_mode = find_resolution(choose_width,choose_height);
	if(!G_mode)
		goto leave;

	if(force_res)
	{
		G_size_x = choose_width;
		G_size_y = choose_height;
		if( G_size_x==0 || G_size_y==0 )
		{
			fprintf(stderr,"when using -f , must supply width and height with -x <val> -y <val>\n");
			ret = 1;
			goto leave;
		}
	}else{
		G_size_x = G_mode->hdisplay;
		G_size_y = G_mode->vdisplay;
	}

	if(!get_prev_crtc())
		goto leave;

	struct framebuffer fb[3];
	fb[0].fd=fb[1].fd=fb[2].fd=-1;
	for(int i=0;i<3;i++)
	{
		if(get_framebuffer(fb+i))
		{
			fprintf(stderr,"Error creating framebuffers\n");
			ret = 4;goto leave;
		}
	}

	if(!get_master())
		return 1;

	ret = 1;
	if(read_stdin)
	{
		if(fill_framebuffer_from_stdin(fb+0))
		{
			ret = 1;
			goto leave;
		}
		memcpy( fb[1].data+8 , fb[0].data , fb[0].dumb_framebuffer.size-8 );
	}else{
		fill_pattern((uint32_t*)fb[0].data,G_size_x,G_size_y,0);
		memcpy( fb[1].data+8 , fb[0].data , fb[0].dumb_framebuffer.size-8 );
	}


	show_framebuffer(fb+0);
//	show_framebuffer(fb+1);

	usleep(100000);
	need_wait_flip = 0;
	waiting = 0;

	int ress[100];
	for(int fl=1200-1;fl>0;fl--)
	{
		fd_set fds;
		drmEventContext evCtx;
		struct timeval tv;

		fill_pattern(
				(uint32_t*)fb[fl&1].data ,
				G_mode->hdisplay ,
				G_mode->vdisplay ,
				fl
		);


		if(need_wait_flip)
		{
			while( waiting )	// waiting flag is reset when the handler is run.
			{
				FD_ZERO(&fds);
				FD_SET(G_drm_dev, &fds);
				evCtx.version = DRM_EVENT_CONTEXT_VERSION;
				evCtx.page_flip_handler = dummy_page_flip_handler;

				tv.tv_sec = 0;
				tv.tv_usec = 500000;
				int status = select(G_drm_dev+1,&fds,0,0,&tv);
				if(status!=1)
				{
					printf("select returns %d\n",status);
					fl+=1000000;
					break;
				}
				//printf(".");
				drmHandleEvent(G_drm_dev,&evCtx);
				printf(",");
				need_wait_flip = 0;
				break;
			}
		}

		if(fl<2)
			break;

		ret = drmModePageFlip( G_drm_dev , G_crtcId , fb[fl&1].buffer_id , DRM_MODE_PAGE_FLIP_EVENT , &waiting );
		if(fl<10)ress[fl] = ret;
		printf(":");
		if(ret)
		{
			fprintf(stderr,"flip returns %d\n",ret);
			break;
		}
		waiting = 1;
		need_wait_flip = 1;


//		memcpy( fb[1].data , fb[0].data , fb[0].dumb_framebuffer.size );
//		usleep(20000);
//		memcpy( fb[1].data+8 , fb[0].data , fb[0].dumb_framebuffer.size-8 );
//		usleep(20000);
	}
	printf("\nloop done.\n");
	fflush(stdout);
	usleep(100000);
	for(int fl=0;fl<10;fl++)
		printf("%d   ",ress[fl]);
	printf("\n");


	//wait_break();

leave:
	for(int i=0;i<3;i++)
	{
		if(fb[i].fd>=0)release_framebuffer(fb+i);
		fb[i].fd=-1;
	}

	if(G_drm_dev>=0)
	{
		restore_prev_crtc();

		release_master();

		if(G_enc)
		{
			drmModeFreeEncoder(G_enc);
			G_enc = 0;
		}
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

static void dummy_page_flip_handler(int fd,unsigned int sequence,unsigned int tv_sec,unsigned int tv_usec,void *user_data)
{
//	*((char*)user_data) = 0;
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

static drmModeModeInfoPtr find_resolution(int w,int h)
{
	drmModeModeInfoPtr res_best;
	int mq;

	// Get the preferred resolution
	drmModeModeInfoPtr resolution = 0;
	mq = -1;
	res_best = 0;
	for (int i=0;i<G_conn->count_modes;i++)
	{
		int q = 0;
		resolution = &G_conn->modes[i];
		if(resolution->type & DRM_MODE_TYPE_PREFERRED)
			q++;
		if( w == resolution->hdisplay )
			q+=2;
		if( h == resolution->vdisplay )
			q+=2;
		if( !res_best || q>mq )
			{res_best=resolution;mq=q;}
	}

	if (!res_best)
	{
		fprintf(stderr,"Could not find a resolution\n");
		return 0;
	}

	printf("%ux%u\n", res_best->hdisplay, res_best->vdisplay);

	return res_best;
}

static char get_prev_crtc()
{
	G_prev_bufId = G_crtc->buffer_id;
	G_prev_mode = G_crtc->mode;
	return 1;
}

static char restore_prev_crtc()
{
	int res;
	res = 0;
	if( G_drm_dev>=0 && G_prev_bufId && G_conn )
	{
		printf(
				"setting previous mode for crtcId %u: bufID=%u, mode %u*%u\n" ,
				(unsigned int)G_crtcId ,
				(unsigned int)G_prev_bufId ,
				(unsigned int)G_prev_mode.hdisplay ,
				(unsigned int)G_prev_mode.vdisplay
		);
		res = drmModeSetCrtc(G_drm_dev,G_crtcId, 0, 0, 0, NULL, 0, NULL);
		res = drmModeSetCrtc(G_drm_dev,G_crtcId,G_prev_bufId,0,0,&(G_conn->connector_id),1,&G_prev_mode);
	}
	return res==0;
}

static char get_master()
{
	int ret;
	if(G_drm_dev<0)
		return 0;
	if(G_master)
		return 1;

	ret=drmSetMaster(G_drm_dev);
	if(ret)
	{
		fprintf(stderr,"could not get DRM master role. res=%d\n",ret);
		return 0;
	}
	G_master=1;
	return 1;
}

static void release_master()
{
	if( G_master && G_drm_dev>=0 )
		drmDropMaster(G_drm_dev);
	G_drm_dev=0;
}

static int fill_framebuffer_from_stdin(struct framebuffer *fb)
{
	size_t total_read = 0;

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

	return 0;
}

static int show_framebuffer(struct framebuffer *fb)
{

	// Make sure we synchronize the display with the buffer. This also works if page flips are enabled
	drmModeSetCrtc(G_drm_dev, G_crtc->crtc_id, 0, 0, 0, NULL, 0, NULL);
	drmModeSetCrtc(G_drm_dev, G_crtc->crtc_id, fb->buffer_id, 0, 0, &G_conn->connector_id, 1, G_mode);

	print_verbose("Sent image to framebuffer\n");

	return 0;
}

static void wait_break()
{
	sigset_t wait_set;
	sigemptyset(&wait_set);
	sigaddset(&wait_set, SIGTERM);
	sigaddset(&wait_set, SIGINT);

	int sig;
	sigprocmask(SIG_BLOCK, &wait_set, NULL );
	sigwait(&wait_set, &sig);
}

