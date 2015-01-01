/* Copyright (c) 2014 Stephen Hurd
* Developers:
* Stephen Hurd (K6BSD/VE5BSD) <shurd@sasktel.net>
*
* Permission is hereby granted, free of charge, to any person obtaining a copy
* of this software and associated documentation files (the "Software"), to deal
* in the Software without restriction, including without limitation the rights
* to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
* copies of the Software, and to permit persons to whom the Software is
* furnished to do so, subject to the following conditions:
*
* The above copyright notice, developer list, and this permission notice shall
* be included in all copies or substantial portions of the Software. If you meet
* us some day, and you think this stuff is worth it, you can buy us a beer in
* return
*
* THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
* IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
* FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
* AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
* LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
* OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
* SOFTWARE.
*/

#include <sockwrap.h>
#ifdef _WIN32
	#include <windows.h>
	#include <mmsystem.h>
#else
	#include <sys/soundcard.h>
	#include <sys/select.h>
#endif
#include <fcntl.h>
#include <inttypes.h>
#include <math.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>

#include <genwrap.h>
#include <comio.h>
#include <threadwrap.h>

enum msg_type {
	MSG_TRANSITION_TO_OFF,
	MSG_TRANSITION_TO_ON,
	MSG_IS_OFF,
	MSG_IS_ON,
	MSG_QUERY
};

protected_int32_t	tone_playing;

#define FREQ	800
#define SRATE	22050
#define WAVE_TPI 6.28318530717958647692

static void tone_thread(void *args)
{
	int			s_len;		// Length in samples of each waveform
	int			s_cycles;	// Number of full wave cycles per sample
	uint8_t		*attack;
	uint8_t		*decay;
	uint8_t		*sustain;
	uint8_t		*silence;
	uint8_t		*next_sample;
	double		period;		// Length of a single wave
	double		frequency;	// Adjusted frequency
	int			i;
	int			tmp;
	double		pos;
	double		inc;
	enum {
		SILENCE,
		ATTACK,
		SUSTAIN,
		DECAY,
	} last_sample = SILENCE;

	period = 1 / (double)(FREQ);
	s_cycles = roundl(0.010 / period);
	if (s_cycles < 1)
		s_cycles = 1;
	s_len = roundl(SRATE * period * s_cycles);
	frequency = (double)SRATE/((double)s_len/(double)s_cycles);
	attack = (uint8_t *)malloc(s_len);
	decay = (uint8_t *)malloc(s_len);
	sustain = (uint8_t *)malloc(s_len);
	silence = (uint8_t *)malloc(s_len);

	// Silence is easy...
	memset(silence, 127, s_len);

	// Now the sustained waveform
	inc = 8.0 * atan(1.0);
	inc *= (frequency / (double)SRATE);
	for (i=0; i<s_len; i++) {
		pos = (inc*(double)i);
		pos -= (int)(pos/WAVE_TPI)*WAVE_TPI;
		sustain[i]=(sin (pos))*127+128;
	}

	// Apply a linear ramp
	for (i=0; i<s_len; i++) {
		pos = (double)i/s_len;
		tmp = sustain[i] - 128;
		attack[i] = (tmp*pos)+128;
		decay[i] = (tmp*(1-pos))+128;
	}

	// Open the wave out thing...
#ifdef _WIN32
	WAVEFORMATEX	w;
	WAVEHDR			wh[2];
	HWAVEOUT		waveOut;
	int				curr_wh=0;

	w.wFormatTag = WAVE_FORMAT_PCM;
	w.nChannels = 1;
	w.nSamplesPerSec = SRATE;
	w.wBitsPerSample = 8;
	w.nBlockAlign = (w.wBitsPerSample * w.nChannels) / 8;
	w.nAvgBytesPerSec = w.nSamplesPerSec * w.nBlockAlign;

	if((i=waveOutOpen(&waveOut, WAVE_MAPPER, &w, 0, 0, 0))!=MMSYSERR_NOERROR) {
		fprintf(stderr, "Unable to open wave mapper\n");
		goto cleanup_nodsp;
	}
	memset(&wh, 0, sizeof(wh));
	wh[0].dwBufferLength=s_len;
	wh[1].dwBufferLength=s_len;
#else
	int dsp;
	int format=AFMT_U8;
	int channels=1;
	int	rate=SRATE;
	int	fragsize=0x0003000a;

	// OSS!  Yay!
	if ((dsp=open("/dev/dsp",O_WRONLY,0))<0) {
		fprintf(stderr, "Unable to open /dev/dsp %d!\n", errno);
		goto cleanup_nodsp;
	}
	if (ioctl(dsp, SNDCTL_DSP_SETFRAGMENT, &fragsize)==-1) {
		fprintf(stderr, "Unable to set fragsize\n");
		goto cleanup;
	}
	if ((ioctl(dsp, SNDCTL_DSP_SETFMT, &format)==-1) || format!=AFMT_U8) {
		fprintf(stderr, "Unable to set format to AFMT_U8\n");
		goto cleanup;
	}
	else if ((ioctl(dsp, SNDCTL_DSP_CHANNELS, &channels)==-1) || channels!=1) {
		fprintf(stderr, "Unable to set single channel\n");
		goto cleanup;
	}
	else if ((ioctl(dsp, SNDCTL_DSP_SPEED, &rate)==-1) || rate!=SRATE) {
		fprintf(stderr, "Unable to set rate to %u\n", SRATE);
		goto cleanup;
	}
#endif

	for (;;) {
		if (protected_int32_value(tone_playing)) {
			switch(last_sample) {
				case SILENCE:
				case DECAY:
					next_sample = attack;
					last_sample = ATTACK;
					break;
				default:
					next_sample = sustain;
					last_sample = SUSTAIN;
					break;
			}
		}
		else {
			switch(last_sample) {
				case ATTACK:
				case SUSTAIN:
					next_sample = decay;
					last_sample = DECAY;
					break;
				default:
					next_sample = silence;
					last_sample = SILENCE;
					break;
			}
		}
#if 0
		if(next_sample == attack)
			printf("A");
		else if(next_sample == decay)
			printf("D\n");
		else if(next_sample == sustain)
			printf(".");
#endif
#ifdef _WIN32
		if(wh[curr_wh].dwFlags & WHDR_PREPARED) {
			while(waveOutUnprepareHeader(waveOut, &wh[curr_wh], sizeof(wh[curr_wh]))==WAVERR_STILLPLAYING)
				SLEEP(1);
		}
		wh[curr_wh].lpData=next_sample;
		wh[curr_wh].dwBufferLength=s_len;
		if(waveOutPrepareHeader(waveOut, &wh[curr_wh], sizeof(wh[curr_wh]))==MMSYSERR_NOERROR) {
			if(waveOutWrite(waveOut, &wh[curr_wh], sizeof(wh[curr_wh]))==MMSYSERR_NOERROR) {
				curr_wh ^= 1;
			}
			else {
				fprintf(stderr, "Unable to write sample %d\n", curr_wh);
				goto cleanup;
			}
		}
		else {
			fprintf(stderr, "Unable to prepare header %d\n", curr_wh);
			goto cleanup;
		}
#else
		int wr=0;
		while(wr<s_len) {
			fd_set	wfd;
			FD_ZERO(&wfd);
			FD_SET(dsp, &wfd);
			if (select(dsp+1, NULL, &wfd, NULL, NULL) >= 0) {
				if (FD_ISSET(dsp, &wfd)) {
					i=write(dsp, next_sample+wr, s_len-wr);
					if(i>=0)
						wr+=i;
					else {
						fprintf(stderr, "write() failure %d\n", errno);
						goto cleanup;
					}
				}
			}
			else {
				fprintf(stderr, "select() failure %d\n", errno);
				goto cleanup;
			}
		}
#endif
	}

cleanup:
#ifdef _WIN32
	waveOutClose(waveOut);
#else
	close(dsp);
#endif
cleanup_nodsp:
	free(attack);
	free(decay);
	free(sustain);
	free(silence);
	return;
}

int main(int argc, char *argv[])
{
	clock_t		last_transition = msclock() - 10000;
	clock_t		new_transition;
	bool		last_DSR;
	bool		new_DSR;
	bool		next_tone_playing = false;
	clock_t		last_remote_transition = msclock() - 10000;
	clock_t		next_remote_transition = 0;
	bool		remote_change_pending = false;
	uint8_t		last_count = 0;
	COM_HANDLE	port;
	int			new_status = 0;
	uint16_t	period;
	uint8_t		count = 0;
	SOCKET		sock;
	struct sockaddr_in	addr;
	char		*com_port = "/dev/ttyv8";
	char		*remote_addr = "rob.synchro.net";
	char		*local_addr = NULL;
	uint8_t		msg[4];
	uint8_t		missed;
	int			i;

	for (i=1; i<argc; i++) {
		if (argv[i][0] != '-')
			goto usage;
		if (i+1 == argc)
			goto usage;
		switch (argv[i][1]) {
			case 'a':
				remote_addr = argv[++i];
				break;
			case 'b':
				local_addr = argv[++i];
				break;
			case 'p':
				com_port = argv[++i];
				break;
		}
	}

	// Touch second base
#ifdef _WINSOCKAPI_
	WSADATA	wsaData;
	if (i=WSAStartup(MAKEWORD(1,1), &wsaData) != 0) {
		fprintf(stderr, "Unable to kick winsock hard enough %d\n", i);
		return 1;
	}
#endif

	if (protected_int32_init(&tone_playing, 0) != 0) {
		fprintf(stderr, "Unable to create protected int\n");
		return 1;
	}

	if (_beginthread(tone_thread, 0, NULL) == -1) {
		fprintf(stderr, "Unable to create tone playing thread\n");
		return 1;
	}

	// Open COM port...
	port = comOpen(com_port);
	if (port == COM_HANDLE_INVALID) {
		fprintf(stderr, "Unable to open COM port %s\n", argv[1]);
		return 1;
	}
	comRaiseDTR(port);

	// Open socket...
	sock = socket(PF_INET, SOCK_DGRAM, IPPROTO_UDP);
	if (sock == SOCKET_ERROR) {
		fprintf(stderr, "Unable to create socket %d\n", ERROR_VALUE);
		return 1;
	}
	// Bind
	memset(&addr, 0, sizeof(addr));
#ifndef _WIN32
	addr.sin_len = sizeof(addr);
#endif
	addr.sin_family = PF_INET;
	addr.sin_port = htons(0xC0DE);
	if (local_addr)
		addr.sin_addr.s_addr = inet_addr(local_addr);
	if (bind(sock, (struct sockaddr *)&addr, sizeof(addr)) != 0) {
		fprintf(stderr, "Unable to bind socket %d\n", ERROR_VALUE);
		return 1;
	}
	// Connect
	addr.sin_addr.s_addr = inet_addr(remote_addr);
	if (connect(sock, (struct sockaddr *)&addr, sizeof(addr)) != 0) {
		fprintf(stderr, "Unable to connect to remote %s %d\n", remote_addr, ERROR_VALUE);
		return 1;
	}
	new_status = 1;
	if (ioctlsocket(sock, FIONBIO, &new_status) != 0) {
		fprintf(stderr, "Unable to set socket non-blocking %d\n", ERROR_VALUE);
		return 1;
	}

	last_DSR = comGetModemStatus(port) & COM_DSR;
	while (1) {
		// First, poll the key
		new_status = comGetModemStatus(port);
		new_DSR = new_status & COM_DSR;
		if (new_DSR != last_DSR) {
			new_transition = msclock();
			if (last_transition > new_transition)
				period = 0;
			else if (last_transition + 10000 /* 10 seconds */ < new_transition)
				period = 0;
			else
				period = new_transition - last_transition;
			last_DSR = new_DSR;
			last_transition = new_transition;
			printf("%c%c\r", new_DSR?'O':'.', protected_int32_value(tone_playing)?'O':'.');
			fflush(stdout);
			msg[0] = count++;
			msg[1] = new_DSR ? MSG_TRANSITION_TO_ON : MSG_TRANSITION_TO_OFF;
			msg[2] = period >> 8;
			msg[3] = period & 0xff;
			if (sendsocket(sock, msg, sizeof(msg)) != sizeof(msg)) {
				fprintf(stderr, "Error sending message %d\n", ERROR_VALUE);
			}
		}
		// Now, change the tone if the time is up...
		if (remote_change_pending) {
			if (msclock() >= next_remote_transition) {
				protected_int32_set(&tone_playing, next_tone_playing);
				remote_change_pending = false;
				printf("%c%c\r", new_DSR?'O':'.', protected_int32_value(tone_playing)?'O':'.');
				fflush(stdout);
			}
		}
		// Now, poll the socket if there's no pending change
		// This breaks the query/is on/is off thing... we should likely use a linked list queue...
		if (!remote_change_pending) {
			switch(recv(sock, msg, sizeof(msg), 0)) {
				case SOCKET_ERROR:
					switch (ERROR_VALUE) {
						case ECONNREFUSED:	// ICMP unreachable (ie: not listening)
						case EAGAIN:		// Nothing pending...
#if (EWOULDBLOCK != EAGAIN)
						case EWOULDBLOCK:
#endif
							break;
						default:
							fprintf(stderr, "Error on recv() %d\n", ERROR_VALUE);
							return 1;
					}
					break;
				case 4:
					// TODO: Check count...
					missed = msg[0] - last_count;
					missed--;
					if (missed) {
						fprintf(stderr, "Missed %hhu messages!\n", missed);
					}
					last_count = msg[0];
					switch(msg[1]) {
						case MSG_TRANSITION_TO_OFF:
							next_tone_playing = false;
							next_remote_transition = last_remote_transition + ((msg[2] << 8)|msg[3]);
							remote_change_pending = true;
							break;
						case MSG_TRANSITION_TO_ON:
							next_tone_playing = true;
							next_remote_transition = last_remote_transition + ((msg[2] << 8)|msg[3]);
							remote_change_pending = true;
							break;
						case MSG_IS_OFF:
							protected_int32_set(&tone_playing, false);
							break;
						case MSG_IS_ON:
							protected_int32_set(&tone_playing, true);
							break;
						case MSG_QUERY:
							msg[0] = count++;
							msg[1] = last_DSR ? MSG_IS_ON : MSG_IS_OFF;
							msg[2] = 0;
							msg[3] = 0;
							sendsocket(sock, msg, sizeof(msg));
							break;
					}
			}
		}
		SLEEP(1);
	}
	return 0;

usage:
	printf("%s -p <COM port> -a <remote IP address> -b <local bind IP address>\n", argv[0]);
	return 0;
}
