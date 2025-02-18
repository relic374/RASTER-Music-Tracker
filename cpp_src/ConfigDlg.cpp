// ConfigDlg.cpp : implementation file
//

#include "stdafx.h"
#include "Rmt.h"
#include "ConfigDlg.h"
#include "FilePathDlg.h"

#ifdef _DEBUG
#define new DEBUG_NEW
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#endif

/////////////////////////////////////////////////////////////////////////////
// CConfigDlg dialog

CConfigDlg::CConfigDlg(CWnd* pParent /*=NULL*/)
	: CDialog(CConfigDlg::IDD, pParent)
{
	//{{AFX_DATA_INIT(CConfigDlg)
	m_midi_TouchResponse = FALSE;
	//m_keyboard_playautofollow = FALSE;
	m_midi_VolumeOffset = 0;
	m_tracklinehighlight = 0;
	m_scaling_percentage = 100;
	//m_tuning = 0;
	m_ntsc = FALSE;
	m_doSmoothScrolling = TRUE;
	m_displayflatnotes = FALSE;
	m_usegermannotation = FALSE;
	m_midi_NoteOff = FALSE;
	m_keyboard_updowncontinue = FALSE;
	m_nohwsoundbuffer = FALSE;
	m_tracklinealtnumbering = FALSE;
	//m_keyboard_swapenter = FALSE;
	m_keyboard_rememberoctavesandvolumes = FALSE;
	m_keyboard_escresetatarisound = FALSE;
	m_keyboard_askwhencontrol_s = FALSE;
	//}}AFX_DATA_INIT
}

void CConfigDlg::DoDataExchange(CDataExchange* pDX)
{
	CDialog::DoDataExchange(pDX);
	//{{AFX_DATA_MAP(CConfigDlg)
	//DDX_Control(pDX, IDC_TRACKCURSORVERTICALRANGE, m_c_trackcursorverticalrange);
	DDX_Control(pDX, IDC_KEYBOARD_LAYOUT, m_keyboard_c_layout);
	DDX_Control(pDX, IDC_MIDI_DEVICE, m_midi_c_device);
	DDX_Check(pDX, IDC_MIDI_TR, m_midi_TouchResponse);
	//DDX_Check(pDX, IDC_KEYBOARD_PLAYAUTOFOLLOW, m_keyboard_playautofollow);
	DDX_Text(pDX, IDC_MIDI_VOLUMEOFFSET, m_midi_VolumeOffset);
	DDV_MinMaxInt(pDX, m_midi_VolumeOffset, 0, 15);
	DDX_Text(pDX, IDC_TRACKLINEHIGHLIGHT, m_tracklinehighlight);
	DDV_MinMaxInt(pDX, m_tracklinehighlight, 2, 256);
	DDX_Text(pDX, IDC_SCALINGPERCENTAGE, m_scaling_percentage);
	DDV_MinMaxInt(pDX, m_scaling_percentage, 100, 300);
	//DDX_Text(pDX, IDC_TUNING, m_tuning);
	//DDV_MinMaxDouble(pDX, m_tuning, 230, 850);	//nearly a full octave accross, should be more than enough
	DDX_Check(pDX, IDC_DISPLAYFLATNOTES, m_displayflatnotes);
	DDX_Check(pDX, IDC_USEGERMANNOTATION, m_usegermannotation);
	DDX_Check(pDX, IDC_NTSC, m_ntsc);
	DDX_Check(pDX, IDC_SMOOTH_SCROLL, m_doSmoothScrolling);
	DDX_Check(pDX, IDC_MIDI_NOTEOFF, m_midi_NoteOff);
	DDX_Check(pDX, IDC_KEYBOARD_UPDOWNCONTINUE, m_keyboard_updowncontinue);
	DDX_Check(pDX, IDC_NOHWSOUNDBUFFER, m_nohwsoundbuffer);
	DDX_Check(pDX, IDC_TRACKLINEALTNUMBERING, m_tracklinealtnumbering);
	//DDX_Check(pDX, IDC_KEYBOARD_SWAPENTER, m_keyboard_swapenter);
	DDX_Check(pDX, IDC_KEYBOARD_REMEMBEROCTAVESANDVOLUMES, m_keyboard_rememberoctavesandvolumes);
	DDX_Check(pDX, IDC_KEYBOARD_ESCRESETATARISOUND, m_keyboard_escresetatarisound);
	DDX_Check(pDX, IDC_KEYBOARD_ASKWHENCONTROL_S, m_keyboard_askwhencontrol_s);
	//}}AFX_DATA_MAP
}

BEGIN_MESSAGE_MAP(CConfigDlg, CDialog)
	//{{AFX_MSG_MAP(CConfigDlg)
	ON_BN_CLICKED(IDC_MIDI_TR, OnMidiTouchResponseClicked)
	ON_CBN_SELCHANGE(IDC_KEYBOARD_LAYOUT, OnSelchangeKeyboardLayout)
	ON_BN_CLICKED(IDC_PATHS, OnPaths)
	//}}AFX_MSG_MAP
END_MESSAGE_MAP()

/////////////////////////////////////////////////////////////////////////////
// CConfigDlg message handlers

BOOL CConfigDlg::OnInitDialog() 
{
	CDialog::OnInitDialog();
	
	// Populate the list with MIDI devices
	m_midi_c_device.AddString("--- none ---");	//id=0

	MIDIINCAPS micaps;
	int numMidiDevices = midiInGetNumDevs();
	for (int i = 0; i < numMidiDevices; i++)
	{
		midiInGetDevCaps(i, &micaps, sizeof(MIDIINCAPS));
		m_midi_c_device.AddString(micaps.szPname);
	}

	m_midi_c_device.SetCurSel(m_midi_device+1);	// +1 because -1 == --- none ---
	
	OnMidiTouchResponseClicked();

	m_keyboard_c_layout.AddString("QWERTY Layout");
	m_keyboard_c_layout.AddString("AZERTY Layout");
	m_keyboard_c_layout.SetCurSel(m_keyboard_layout);

	return TRUE;
}

/// <summary>
/// Apply the configuration changes not handled by the DoDataExchange function
/// MIDI device combo
/// Keybord layout combo
/// </summary>
void CConfigDlg::OnOK() 
{
	m_midi_device = m_midi_c_device.GetCurSel()-1;
	m_keyboard_layout = m_keyboard_c_layout.GetCurSel();
	CDialog::OnOK();
}

/// <summary>
/// If MIDI touch response if turned on then enable the MIDI volume offset edit box
/// </summary>
void CConfigDlg::OnMidiTouchResponseClicked() 
{
	CButton *tr = (CButton*)GetDlgItem(IDC_MIDI_TR);
	CEdit *vof = (CEdit*)GetDlgItem(IDC_MIDI_VOLUMEOFFSET);
	vof->EnableWindow(tr->GetCheck());
}

void CConfigDlg::OnSelchangeKeyboardLayout() 
{
	if (m_keyboard_c_layout.GetCurSel()==1)
	{
		//Layer 2
		CButton* af = (CButton*)GetDlgItem(IDC_KEYBOARD_PLAYAUTOFOLLOW);
		af->SetCheck(1);
	}
}

void CConfigDlg::OnPaths() 
{
	CConfigPathsDlg dlg;
	dlg.m_path_songs = g_defaultSongsPath;
	dlg.m_path_instruments = g_defaultInstrumentsPath;
	dlg.m_path_tracks = g_defaultTracksPath;
	if (dlg.DoModal()==IDOK)
	{
		g_defaultSongsPath = dlg.m_path_songs;
		g_defaultInstrumentsPath = dlg.m_path_instruments;
		g_defaultTracksPath = dlg.m_path_tracks;
		g_lastLoadPath_Songs = g_lastLoadPath_Instruments = g_lastLoadPath_Tracks = "";
	}
}
/////////////////////////////////////////////////////////////////////////////
// CConfigPathsDlg dialog


CConfigPathsDlg::CConfigPathsDlg(CWnd* pParent /*=NULL*/)
	: CDialog(CConfigPathsDlg::IDD, pParent)
{
	//{{AFX_DATA_INIT(CConfigPathsDlg)
	m_path_songs = _T("");
	m_path_instruments = _T("");
	m_path_tracks = _T("");
	//}}AFX_DATA_INIT
}


void CConfigPathsDlg::DoDataExchange(CDataExchange* pDX)
{
	CDialog::DoDataExchange(pDX);
	//{{AFX_DATA_MAP(CConfigPathsDlg)
	DDX_Text(pDX, IDC_EDIT1, m_path_songs);
	DDX_Text(pDX, IDC_EDIT2, m_path_instruments);
	DDX_Text(pDX, IDC_EDIT3, m_path_tracks);
	//}}AFX_DATA_MAP
}


BEGIN_MESSAGE_MAP(CConfigPathsDlg, CDialog)
	//{{AFX_MSG_MAP(CConfigPathsDlg)
	ON_BN_CLICKED(IDC_BUTTON1, OnButton1)
	ON_BN_CLICKED(IDC_BUTTON2, OnButton2)
	ON_BN_CLICKED(IDC_BUTTON3, OnButton3)
	//}}AFX_MSG_MAP
END_MESSAGE_MAP()

/////////////////////////////////////////////////////////////////////////////
// CConfigPathsDlg message handlers

void CConfigPathsDlg::BrowsePath(int itemID) 
{
	CFilePathDlg dlg;
	CString s;
	((CWnd*)GetDlgItem(itemID))->GetWindowText(s);
	dlg.m_path=s;
	if (dlg.DoModal()==IDOK)
	{
		((CWnd*)GetDlgItem(itemID))->SetWindowText(dlg.m_path);
	}
}

void CConfigPathsDlg::OnButton1() 
{
	BrowsePath(IDC_EDIT1);
}

void CConfigPathsDlg::OnButton2() 
{
	BrowsePath(IDC_EDIT2);
}

void CConfigPathsDlg::OnButton3() 
{
	BrowsePath(IDC_EDIT3);	
}
