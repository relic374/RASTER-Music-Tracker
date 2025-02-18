// Original code by Raster, 2002-2009
// Experimental changes and additions by VinsCool, 2021-2022
// TODO: fix apokeysnd support, and backport sapokey changes to alternative plugins

#include "stdafx.h"
#include "Rmt.h"
#include "XPokey.h"
#include "RmtView.h"
#include "Atari6502.h"
#include "global.h"
#include "ChannelControl.h"
#include "PokeyStream.h"

#ifdef _DEBUG
#define new DEBUG_NEW
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#endif

typedef enum 
{
	ASAP_FORMAT_U8 = 8,       /* unsigned char */
	ASAP_FORMAT_S16_LE = 16,  /* signed short, little-endian */
	ASAP_FORMAT_S16_BE = -16  /* signed short, big-endian */
} ASAP_SampleFormat;

typedef int abool;

typedef void (* APokeySound_Initialize_PROC)(abool stereo);
typedef void (* APokeySound_PutByte_PROC)(int addr, int data);
typedef int  (* APokeySound_GetRandom_PROC)(int addr, int cycle);
typedef int  (* APokeySound_Generate_PROC)(int cycles, byte buffer[], ASAP_SampleFormat format);
typedef void (* APokeySound_About_PROC)(const char **name, const char **author, const char **description);

APokeySound_Initialize_PROC APokeySound_Initialize;
APokeySound_PutByte_PROC APokeySound_PutByte;
APokeySound_GetRandom_PROC APokeySound_GetRandom;
APokeySound_Generate_PROC APokeySound_Generate;
APokeySound_About_PROC APokeySound_About;

typedef void (* Pokey_Initialise_PROC)(int*, char**);
typedef void (* Pokey_SoundInit_PROC)(DWORD, WORD, BYTE);
typedef void (* Pokey_Process_PROC)(BYTE*, const WORD);
typedef BYTE (* Pokey_GetByte_PROC)(WORD);
typedef void (* Pokey_PutByte_PROC)(WORD, BYTE);
typedef void (* Pokey_About_PROC)(char**, char**, char**);

Pokey_Initialise_PROC Pokey_Initialise;
Pokey_SoundInit_PROC Pokey_SoundInit;
Pokey_Process_PROC Pokey_Process;
Pokey_GetByte_PROC Pokey_GetByte;
Pokey_PutByte_PROC Pokey_PutByte;
Pokey_About_PROC Pokey_About;

// Needed for proper Stereo detection with POKEY plugins
int numTracksSetOnDriver = g_tracks4_8;

static LPDIRECTSOUND          g_lpds;
static LPDIRECTSOUNDBUFFER    g_lpdsbPrimary;

extern CPokeyStream g_PokeyStream;

int loops = 0;			// TODO: Take out

CXPokey::CXPokey()
{
	m_soundDriverId = SOUND_DRIVER_NONE;
	m_pokey_dll = NULL;
	m_SoundBuffer = NULL;
}

CXPokey::~CXPokey()
{
	DeInitSound();
}

BOOL CXPokey::DeInitSound()
{
	m_soundDriverId = SOUND_DRIVER_NONE;
	if (m_pokey_dll)
	{
		FreeLibrary(m_pokey_dll);
		m_pokey_dll = NULL;
	}
	g_aboutpokey = "No Pokey sound emulation.";

	if (m_SoundBuffer)
	{
		m_SoundBuffer->Stop();
		m_SoundBuffer->Release();
	}
	m_SoundBuffer = NULL;

	if (g_lpdsbPrimary) g_lpdsbPrimary->Release();
	g_lpdsbPrimary = NULL;

	//if (g_lpds) g_lpds->Release();	//this seems to work around creating additional buffers over and over
	g_lpds = NULL;

	return 1;
}

BOOL CXPokey::ReInitSound()
{
	DeInitSound();
	return InitSound();
}

BOOL CXPokey::RenderSound1_50(int instrspeed)
{
	if (!m_soundDriverId) return 0;
	if (!m_SoundBuffer) return 0;

	m_SoundBuffer->GetCurrentPosition(&m_PlayCursor,&m_WriteCursor);

	//|||||||||||||||||||||||||||||||||||||||||
	//           ^|-------delta------->^
	//      m_WriteCursor           m_LoadPos

	int delta = (m_LoadPos - m_WriteCursor) & (BUFFER_SIZE-1);
	int CHUNK_SIZE = (g_ntsc) ? CHUNK_SIZE_NTSC : CHUNK_SIZE_PAL;

	m_LoadSize = CHUNK_SIZE;	//1764  (882 samples * 2 channels)
	if ( delta > LATENCY_SIZE )
	{
		if (delta > (BUFFER_SIZE/2))
		{
			//we missed it, we're more than half a buffer late, so get to it and move on to what we should be right
			m_LoadPos = (m_WriteCursor+LATENCY_SIZE) & (BUFFER_SIZE-1);
		}
		else 
		{
			//we ran too far ahead so we slow down a bit (we will render smaller pieces than CHUNK_SIZE)
			m_LoadSize = CHUNK_SIZE - ( ((delta-LATENCY_SIZE) /16) & (BUFFER_SIZE-1-1) );	//-1-1 <=Just the numbers!
			//watched
			if (m_LoadSize <= 0 ) return 0; //we are so far ahead that it will not render at all
		}
	}
	else // delta <=LATENCY_SIZE
	{
		//we are closer than the required latency, that's great, but we'd rather speed up so that m_WriteCursor doesn't catch up with us (we'll render bigger chunks than CHUNK_SIZE)
		m_LoadSize = CHUNK_SIZE + ( ((LATENCY_SIZE-delta) /16) & (BUFFER_SIZE-1-1) );	//-1-1 <=Just the numbers!

		//watched
		if (m_LoadSize >= BUFFER_SIZE/2 ) return 0; //that would be a bigger piece than the size of a buffer pulse
	}

	int rendersize=m_LoadSize;
	int renderpartsize;
	int renderoffset=0;

	for (; instrspeed > 0; instrspeed--)
	{
		//--- RMT - instrument play ---/
		if (g_rmtroutine) Atari_PlayRMT();	//one run RMT routine (instruments)
		MemToPokey(g_tracks4_8);			//transfer from g_atarimem to POKEY (mono or stereo)
		renderpartsize = (rendersize / instrspeed) & 0xfffe;	//just the numbers

		if (m_soundDriverId == SOUND_DRIVER_APOKEYSND)
		{
			// apokeysnd
			int CYCLESPERSECOND = (g_ntsc) ? FREQ_17_NTSC : FREQ_17_PAL;
			int FRAMERATE = (g_ntsc) ? FRAMERATE_NTSC : FRAMERATE_PAL;
			int cycles = (unsigned short)((float)renderpartsize / CHANNELS * CYCLESPERSAMPLE);
			while (cycles > 0 && renderpartsize > 0)
			{
				// The maximum number of cycles that can be generated is CYCLESPERSCREEN
				int rencyc = (cycles > CYCLESPERSCREEN) ? (int)CYCLESPERSCREEN : cycles;
				renderpartsize = APokeySound_Generate(rencyc, (unsigned char*)&m_PlayBuffer + renderoffset, ASAP_FORMAT_U8);
				rendersize -= renderpartsize;
				renderoffset += renderpartsize;
				cycles -= rencyc;
			}
		}
		else
		if (m_soundDriverId == SOUND_DRIVER_SA_POKEY)
		{
			// sa_pokey
			Pokey_Process((unsigned char*)&m_PlayBuffer + renderoffset, (unsigned short)renderpartsize);
			rendersize -= renderpartsize;
			renderoffset += renderpartsize;
		}
	}

	if (!m_SoundBuffer) return 0;	//should help preventing crashes from reading NULL pointer when data is read faster than it could be processed

	m_LoadSize = renderoffset; //actually generated sample data

	int r = m_SoundBuffer->Lock(m_LoadPos, m_LoadSize, &Data1, &dwSize1, &Data2, &dwSize2, 0);

	if (r == DS_OK)
	{
		// Render the whole m_LoadSize into the buffer at once
		m_LoadPos = (m_LoadPos + m_LoadSize) & (BUFFER_SIZE - 1);

		// Transfer the first bit from the buffer to Data1
		memcpy(Data1, m_PlayBuffer, dwSize1);

		if (Data2)
		{
			// If it is divided, now transfer the remaining bits
			memcpy(Data2, m_PlayBuffer + dwSize1, dwSize2);
		}
		m_SoundBuffer->Unlock(Data1, dwSize1, Data2, dwSize2);

		return 1;
	}

	return 0;
}

BOOL CXPokey::InitSound()
{
	if (m_soundDriverId || m_pokey_dll) DeInitSound();	//just in case

	int pokeyType = SOUND_DRIVER_NONE;
	int CHUNK_SIZE = (g_ntsc) ? CHUNK_SIZE_NTSC : CHUNK_SIZE_PAL;
	CString warningMessage = "";

	m_pokey_dll = LoadLibrary("apokeysnd.dll");

	if (m_pokey_dll)
	{
		// apokeysnd.dll
		APokeySound_Initialize = (APokeySound_Initialize_PROC)GetProcAddress(m_pokey_dll, "APokeySound_Initialize");
		if (!APokeySound_Initialize) warningMessage += "APokeySound_Initialize\n";

		APokeySound_PutByte = (APokeySound_PutByte_PROC)GetProcAddress(m_pokey_dll, "APokeySound_PutByte");
		if (!APokeySound_PutByte) warningMessage += "APokeySound_PutByte\n";

		APokeySound_GetRandom = (APokeySound_GetRandom_PROC)GetProcAddress(m_pokey_dll, "APokeySound_GetRandom");
		if (!APokeySound_GetRandom) warningMessage += "APokeySound_GetRandom\n";

		APokeySound_Generate = (APokeySound_Generate_PROC)GetProcAddress(m_pokey_dll, "APokeySound_Generate");
		if (!APokeySound_Generate) warningMessage += "APokeySound_Generate\n";

		APokeySound_About = (APokeySound_About_PROC)GetProcAddress(m_pokey_dll, "APokeySound_About");
		if (!APokeySound_About) warningMessage += "APokeySound_About\n";

		if (!warningMessage.IsEmpty())
		{
			MessageBox(g_hwnd, "Error:\nNo compatible 'apokeysnd.dll',\ntherefore the Pokey sound can't be performed.\nIncompatibility with:" + warningMessage, "Pokey library error", MB_ICONEXCLAMATION);
			DeInitSound();
			return 1;
		}
		// Get "About" data from apokeysnd driver
		const char* name, * author, * description;
		APokeySound_About(&name, &author, &description);
		g_aboutpokey.Format("%s\n%s\n%s", name, author, description);
		APokeySound_Initialize(1);	// STEREO enabled
		pokeyType = SOUND_DRIVER_APOKEYSND;
	}
	else
	{
		// Not apokeysnd.dll, so try "sa_pokey.dll"
		m_pokey_dll = LoadLibrary("sa_pokey.dll");
		if (!m_pokey_dll)
		{
			// Not sa_pokey.dll either
			MessageBox(g_hwnd, "Warning:\nNone of 'apokeysnd.dll' or 'sa_pokey.dll' found,\ntherefore the Pokey sound can't be performed.", "LoadLibrary error", MB_ICONEXCLAMATION);
			DeInitSound();
			return 1;
		}

		// sa_pokey.dll
		Pokey_Initialise = (Pokey_Initialise_PROC)GetProcAddress(m_pokey_dll, "Pokey_Initialise");
		if (!Pokey_Initialise) warningMessage += "Pokey_Initialise\n";

		Pokey_SoundInit = (Pokey_SoundInit_PROC)GetProcAddress(m_pokey_dll, "Pokey_SoundInit");
		if (!Pokey_SoundInit) warningMessage += "Pokey_SoundInit\n";

		Pokey_Process = (Pokey_Process_PROC)GetProcAddress(m_pokey_dll, "Pokey_Process");
		if (!Pokey_Process) warningMessage += "Pokey_Process\n";

		Pokey_GetByte = (Pokey_GetByte_PROC)GetProcAddress(m_pokey_dll, "Pokey_GetByte");
		if (!Pokey_GetByte) warningMessage += "Pokey_GetByte\n";

		Pokey_PutByte = (Pokey_PutByte_PROC)GetProcAddress(m_pokey_dll, "Pokey_PutByte");
		if (!Pokey_PutByte) warningMessage += "Pokey_PutByte\n";

		Pokey_About = (Pokey_About_PROC)GetProcAddress(m_pokey_dll, "Pokey_About");
		if (!Pokey_About) warningMessage += "Pokey_About\n";

		if (!warningMessage.IsEmpty())
		{
			MessageBox(g_hwnd, "Error:\nNo compatible 'sa_pokey.dll',\ntherefore the Pokey sound can't be performed.\nIncompatibility with:" + warningMessage, "Pokey library error", MB_ICONEXCLAMATION);
			DeInitSound();
			return 1;
		}

		// Get "About" data from sa_pokey driver
		char* name, * author, * description;
		Pokey_About(&name, &author, &description);
		g_aboutpokey.Format("%s\n%s\n%s", name, author, description);
		Pokey_Initialise(0, 0);

		// Specify the machine region and if it uses Stereo or Mono.
		// This was originally implemented specifically for Altirra's POKEY sound emulation plugins, which also had to be modified to respond to these parameters accordingly
		// other plugins can benefit from these changes if they are also updated to match this different setup
		int FREQ_17 = (g_ntsc) ? FREQ_17_NTSC : FREQ_17_PAL;
		int WANT_STEREO = (g_tracks4_8 == 8) ? 1 : 0;

		// Send these value to the Pokey_SoundInit procedure, if the plugin code was updated accordingly, this should work exactly as expected, and could be changed at any time.
		Pokey_SoundInit(FREQ_17, OUTPUTFREQ, WANT_STEREO);

		pokeyType = SOUND_DRIVER_SA_POKEY;
	}

	WAVEFORMATEX wfm;

	if (DirectSoundCreate(NULL, &g_lpds, NULL) != DS_OK)
	{
		MessageBox(g_hwnd, "Error: DirectSoundCreate", "DirectSound Error!", MB_OK | MB_ICONSTOP);
		return FALSE;
	}

	// Set cooperative level
	if (g_lpds->SetCooperativeLevel(AfxGetApp()->GetMainWnd()->m_hWnd, DSSCL_PRIORITY) != DS_OK)
	{
		MessageBox(g_hwnd, "Error: SetCooperativeLevel", "DirectSound Error!", MB_OK | MB_ICONSTOP);
		return FALSE;
	}

	// Set primary buffer format
	ZeroMemory(&wfm, sizeof(WAVEFORMATEX));
	wfm.wFormatTag = WAVE_FORMAT_PCM;
	wfm.nChannels = CHANNELS;			//2
	wfm.nSamplesPerSec = OUTPUTFREQ;	//44100
	wfm.wBitsPerSample = BITRESOLUTION;	//8 
	wfm.nBlockAlign = wfm.wBitsPerSample / 8 * wfm.nChannels;
	wfm.nAvgBytesPerSec = wfm.nSamplesPerSec * wfm.nBlockAlign;
	wfm.cbSize = 0;

	// Create primary buffer.
	ZeroMemory(&dsbdesc, sizeof(DSBUFFERDESC));
	dsbdesc.dwSize = sizeof(DSBUFFERDESC);
	dsbdesc.dwFlags = DSBCAPS_PRIMARYBUFFER;

	if (g_lpds->CreateSoundBuffer(&dsbdesc, &g_lpdsbPrimary, NULL) != DS_OK)
	{
		MessageBox(g_hwnd, "Error: CreatePrimarySoundBuffer", "DirectSound Error!", MB_OK | MB_ICONSTOP);
		return FALSE;
	}

	if (g_lpdsbPrimary->SetFormat(&wfm) != DS_OK)
	{
		MessageBox(g_hwnd, "Error: SetFormat", "DirectSound Error!", MB_OK | MB_ICONSTOP);
		return FALSE;
	}

	ZeroMemory(&dsbdesc, sizeof(DSBUFFERDESC));
	dsbdesc.dwSize = sizeof(DSBUFFERDESC);
	dsbdesc.dwFlags = DSBCAPS_GETCURRENTPOSITION2 | DSBCAPS_LOCHARDWARE | DSBCAPS_GLOBALFOCUS | DSBCAPS_STICKYFOCUS;
	dsbdesc.dwBufferBytes = BUFFER_SIZE;
	dsbdesc.lpwfxFormat = &wfm;

	if (g_nohwsoundbuffer ||
		g_lpds->CreateSoundBuffer(&dsbdesc, &m_SoundBuffer, NULL) != DS_OK)
	{
		dsbdesc.dwFlags = DSBCAPS_GETCURRENTPOSITION2 | DSBCAPS_LOCSOFTWARE | DSBCAPS_GLOBALFOCUS | DSBCAPS_STICKYFOCUS;
		if (g_lpds->CreateSoundBuffer(&dsbdesc, &m_SoundBuffer, NULL) != DS_OK)
		{
			MessageBox(g_hwnd, "Error: CreateSoundBuffer", "DirectSound Error!", MB_OK | MB_ICONSTOP);
			return FALSE;
		}
	}

	DSBCAPS bc;
	bc.dwSize = sizeof(bc);
	m_SoundBuffer->GetCaps(&bc);

	int r = m_SoundBuffer->Lock(0, BUFFER_SIZE, &Data1, &dwSize1, &Data2, &dwSize2, DSBLOCK_FROMWRITECURSOR);
	if (r == DS_OK)
	{
		memset(Data1, 0, dwSize1);
		if (Data2) memset(Data2, 0, dwSize2);
		m_SoundBuffer->Unlock(Data1, dwSize1, Data2, dwSize2);
	}

	m_SoundBuffer->Play(0, 0, DSBPLAY_LOOPING);
	m_SoundBuffer->GetCurrentPosition(&m_PlayCursor, &m_WriteCursorStart);
	m_LoadPos = (m_WriteCursorStart + LATENCY_SIZE) & (BUFFER_SIZE - 1);  //initial latency (in hundredths of a second)
	m_soundDriverId = pokeyType;

	return 1;
}

/// <summary>
/// Transfer 9 Pokey registers values into the sound driver.
/// Mono: D200-D208
/// Stereo: D200-D208 & D210-D218
/// </summary>
/// <param name="howManyTracks">4 or 8</param>
void CXPokey::MemToPokey(int howManyTracks)
{
	if (numTracksSetOnDriver != g_tracks4_8)	//must be reset if the channels number is mismatched! This was added specifically to autodetect Mono/Stereo configuration with POKEY plugins
	{
		ReInitSound();
		numTracksSetOnDriver = g_tracks4_8;
		return;
	}

	g_PokeyStream.Record();	

	if (m_soundDriverId == SOUND_DRIVER_APOKEYSND)
	{
		//apokey
		for (int i = 0; i <= 8; i++)	// 0-7 + 8audctl
		{
			APokeySound_PutByte(i,(i&0x01) && !GetChannelOnOff(i/2)? 0 : g_atarimem[0xd200+i]);
			if (howManyTracks == 8) 
				APokeySound_PutByte(i+16,(i&0x01) && !GetChannelOnOff(i/2+4)? 0 : g_atarimem[0xd210+i]);	//stereo
		}
	}
	else
	if (m_soundDriverId == SOUND_DRIVER_SA_POKEY)
	{
		//sa_pokey
		for (int i = 0; i <= 8; i++)	//0-7 + 8audctl
		{
			Pokey_PutByte(i,(i&0x01) && !GetChannelOnOff(i/2)? 0 : g_atarimem[0xd200+i]);
			if (howManyTracks == 8) 
				Pokey_PutByte(i+16,(i&0x01) && !GetChannelOnOff(i/2+4)? 0 : g_atarimem[0xd210+i]);		//stereo
		}
	}
}