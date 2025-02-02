/*
Copyright (c) 2012, Broadcom Europe Ltd
All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are met:
    * Redistributions of source code must retain the above copyright
      notice, this list of conditions and the following disclaimer.
    * Redistributions in binary form must reproduce the above copyright
      notice, this list of conditions and the following disclaimer in the
      documentation and/or other materials provided with the distribution.
    * Neither the name of the copyright holder nor the
      names of its contributors may be used to endorse or promote products
      derived from this software without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY
DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
(INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
(INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/

// Video deocode demo using OpenMAX IL though the ilcient helper library

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "bcm_host.h"
#include "ilclient.h"

//static int video_decode_test(int rot, int ox, int oy, int sx, int sy )
static int video_decode_test(int rot, int ox, int oy, int sx)
{
	OMX_VIDEO_PARAM_PORTFORMATTYPE format;
	OMX_TIME_CONFIG_CLOCKSTATETYPE cstate;
	COMPONENT_T *video_decode = NULL, *video_scheduler = NULL, *video_render = NULL, *clock = NULL;
	COMPONENT_T *list[5];
	TUNNEL_T tunnel[4];
	ILCLIENT_T *client;
	int status = 0;
	unsigned int data_len = 0;

	memset(list, 0, sizeof(list));
	memset(tunnel, 0, sizeof(tunnel));

	if((client = ilclient_init()) == NULL)
	{
		return -3;
	}

	if(OMX_Init() != OMX_ErrorNone)
	{
		ilclient_destroy(client);
		return -4;
	}

	// create video_decode
	if(ilclient_create_component(client, &video_decode, "video_decode", ILCLIENT_DISABLE_ALL_PORTS | ILCLIENT_ENABLE_INPUT_BUFFERS) != 0)
		status = -14;
	list[0] = video_decode;

	// create video_render
	if(status == 0 && ilclient_create_component(client, &video_render, "video_render", ILCLIENT_DISABLE_ALL_PORTS) != 0)
		status = -14;

	OMX_CONFIG_DISPLAYREGIONTYPE configDisplay;
	memset(&configDisplay, 0, sizeof configDisplay);
	configDisplay.nSize = sizeof configDisplay;
	configDisplay.nVersion.nVersion = OMX_VERSION;
	configDisplay.nPortIndex = 90;



	list[1] = video_render;

	// create clock
	if(status == 0 && ilclient_create_component(client, &clock, "clock", ILCLIENT_DISABLE_ALL_PORTS) != 0)
		status = -14;
	list[2] = clock;

	memset(&cstate, 0, sizeof(cstate));
	cstate.nSize = sizeof(cstate);
	cstate.nVersion.nVersion = OMX_VERSION;
	cstate.eState = OMX_TIME_ClockStateWaitingForStartTime;
	cstate.nWaitMask = 1;
	if(clock != NULL && OMX_SetParameter(ILC_GET_HANDLE(clock), OMX_IndexConfigTimeClockState, &cstate) != OMX_ErrorNone)
		status = -13;

	// create video_scheduler
	if(status == 0 && ilclient_create_component(client, &video_scheduler, "video_scheduler", ILCLIENT_DISABLE_ALL_PORTS) != 0)
		status = -14;
	list[3] = video_scheduler;

	set_tunnel(tunnel, video_decode, 131, video_scheduler, 10);
	set_tunnel(tunnel+1, video_scheduler, 11, video_render, 90);
	set_tunnel(tunnel+2, clock, 80, video_scheduler, 12);

	// setup clock tunnel first
	if(status == 0 && ilclient_setup_tunnel(tunnel+2, 0, 0) != 0)
		status = -15;
	else
		ilclient_change_component_state(clock, OMX_StateExecuting);

	if(status == 0)
		ilclient_change_component_state(video_decode, OMX_StateIdle);

	memset(&format, 0, sizeof(OMX_VIDEO_PARAM_PORTFORMATTYPE));
	format.nSize = sizeof(OMX_VIDEO_PARAM_PORTFORMATTYPE);
	format.nVersion.nVersion = OMX_VERSION;
	format.nPortIndex = 130;
	format.eCompressionFormat = OMX_VIDEO_CodingAVC;
	format.xFramerate = 60 << 16;

	if(status == 0 &&
		OMX_SetParameter(ILC_GET_HANDLE(video_decode), OMX_IndexParamVideoPortFormat, &format) == OMX_ErrorNone &&
		ilclient_enable_port_buffers(video_decode, 130, NULL, NULL, NULL) == 0)
	{
		OMX_BUFFERHEADERTYPE *buf;
		int port_settings_changed = 0;
		int first_packet = 1;

		ilclient_change_component_state(video_decode, OMX_StateExecuting);

		while((buf = ilclient_get_input_buffer(video_decode, 130, 1)) != NULL)
		{
			// feed data and wait until we get port settings changed
			unsigned char *dest = buf->pBuffer;

			data_len = read(STDIN_FILENO, dest, buf->nAllocLen-data_len);


			if(port_settings_changed == 0 &&
			((data_len > 0 && ilclient_remove_event(video_decode, OMX_EventPortSettingsChanged, 131, 0, 0, 1) == 0) ||
			(data_len == 0 && ilclient_wait_for_event(video_decode, OMX_EventPortSettingsChanged, 131, 0, 0, 1,
			ILCLIENT_EVENT_ERROR | ILCLIENT_PARAMETER_CHANGED, 10000) == 0)))
			{
				port_settings_changed = 1;

				if(ilclient_setup_tunnel(tunnel, 0, 0) != 0)
				{
					status = -7;
					break;
				}

				ilclient_change_component_state(video_scheduler, OMX_StateExecuting);

				// now setup tunnel to video_render
				if(ilclient_setup_tunnel(tunnel+1, 0, 1000) != 0)
				{
					status = -12;
					break;
				}

				ilclient_change_component_state(video_render, OMX_StateExecuting);

				// Get video size ---
				OMX_PARAM_PORTDEFINITIONTYPE portdef;
				portdef.nSize = sizeof(OMX_PARAM_PORTDEFINITIONTYPE);
				portdef.nVersion.nVersion = OMX_VERSION;
				portdef.nPortIndex = 131;
				OMX_GetParameter(ILC_GET_HANDLE(video_decode), OMX_IndexParamPortDefinition, &portdef);

				float ratio = (float)portdef.format.video.nFrameWidth / (float)portdef.format.video.nFrameHeight;
				int tmpx = 0;
				int tmpy = 0;
				//printf("Width: %d, Height: %d, Ratio: %f\n", portdef.format.video.nFrameWidth, portdef.format.video.nFrameHeight, ratio);
				if(sx == 0) {
					configDisplay.set = (OMX_DISPLAYSETTYPE)(OMX_DISPLAY_SET_TRANSFORM | OMX_DISPLAY_SET_LAYER | OMX_DISPLAY_SET_NUM);
				}else{
					
					switch (rot)
					{
						case 'r' : tmpx = sx / ratio; tmpy = sx; break;
						case 'i' : tmpx = sx; tmpy = sx / ratio; break;
						case 'l' : tmpx = sx / ratio; tmpy = sx; break;
						case 'n' : tmpx = sx; tmpy = sx / ratio; break;
						default  : tmpx = sx; tmpy = sx / ratio;
					}
					//printf("Width: %d, Height: %d, Ratio: %f\n", tmpx, tmpy, ratio);
					configDisplay.fullscreen = OMX_FALSE;
					configDisplay.noaspect   = OMX_TRUE;
					configDisplay.set = (OMX_DISPLAYSETTYPE)(OMX_DISPLAY_SET_TRANSFORM | OMX_DISPLAY_SET_LAYER | OMX_DISPLAY_SET_NUM | OMX_DISPLAY_SET_DEST_RECT | OMX_DISPLAY_SET_SRC_RECT | OMX_DISPLAY_SET_FULLSCREEN | OMX_DISPLAY_SET_NOASPECT);
					
					if(rot == 'r') 
						configDisplay.dest_rect.x_offset  = ox - portdef.format.video.nFrameHeight;
					else
						configDisplay.dest_rect.x_offset  = ox;
					if(rot == 'i')
						configDisplay.dest_rect.y_offset  = oy - portdef.format.video.nFrameHeight;
					else
						configDisplay.dest_rect.y_offset  = oy;
					configDisplay.dest_rect.width     = tmpx;
					configDisplay.dest_rect.height    = tmpy;
				}

				configDisplay.num = 1;
				configDisplay.layer = (1<<15)-1;
				switch (rot)
				{
					case 'r' : configDisplay.transform = OMX_DISPLAY_ROT90; break;
					case 'i' : configDisplay.transform = OMX_DISPLAY_ROT180; break;
					case 'l' : configDisplay.transform = OMX_DISPLAY_ROT270; break;
					case 'n' : configDisplay.transform = OMX_DISPLAY_ROT0; break;
					default  : configDisplay.transform = OMX_DISPLAY_ROT0;
				}

				if (OMX_SetConfig(ILC_GET_HANDLE(video_render), OMX_IndexConfigDisplayRegion, &configDisplay) != OMX_ErrorNone)
					status = -15;
			}
			if(!data_len)
				break;

			buf->nFilledLen = data_len;
			data_len = 0;

			buf->nOffset = 0;
			if(first_packet)
			{
				buf->nFlags = OMX_BUFFERFLAG_STARTTIME;
				first_packet = 0;
			}
			else
				buf->nFlags = OMX_BUFFERFLAG_TIME_UNKNOWN;

			if(OMX_EmptyThisBuffer(ILC_GET_HANDLE(video_decode), buf) != OMX_ErrorNone)
			{
				status = -6;
				break;
			}
		}

		buf->nFilledLen = 0;
		buf->nFlags = OMX_BUFFERFLAG_TIME_UNKNOWN | OMX_BUFFERFLAG_EOS;

		if(OMX_EmptyThisBuffer(ILC_GET_HANDLE(video_decode), buf) != OMX_ErrorNone)
		 status = -20;

		// wait for EOS from render
		ilclient_wait_for_event(video_render, OMX_EventBufferFlag, 90, 0, OMX_BUFFERFLAG_EOS, 0,
							  ILCLIENT_BUFFER_FLAG_EOS, 10000);

		// need to flush the renderer to allow video_decode to disable its input port
		ilclient_flush_tunnels(tunnel, 0);

		ilclient_disable_port_buffers(video_decode, 130, NULL, NULL, NULL);
	}


	ilclient_disable_tunnel(tunnel);
	ilclient_disable_tunnel(tunnel+1);
	ilclient_disable_tunnel(tunnel+2);
	ilclient_teardown_tunnels(tunnel);

	ilclient_state_transition(list, OMX_StateIdle);
	ilclient_state_transition(list, OMX_StateLoaded);

	ilclient_cleanup_components(list);

	OMX_Deinit();

	ilclient_destroy(client);
	return status;
}

int main (int argc, char **argv)
{
	
	char rot = 'n';
	int ox = 0;
	int oy = 0;
	int sx = 0;
	//int sy = 0;
	
	if(argc > 1) {
		rot = argv[1][0];
	}
	printf("rotation: %c\n", rot);

	if((argc > 2) && (argc = 5)){
		ox = atoi(argv[2]);
		oy = atoi(argv[3]);
		sx = atoi(argv[4]);
		//sy = atoi(argv[5]);
	}

	//printf("ox: %d oy: %d sx: %d sy: %d\n", ox, oy, sx, sy);
	printf("ox: %d oy: %d sx: %d\n", ox, oy, sx);
	
	bcm_host_init();
	
	//return video_decode_test(rot, ox, oy, sx, sy);
	return video_decode_test(rot, ox, oy, sx);
	
	//return 0;

}


