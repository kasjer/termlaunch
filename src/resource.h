// Resource IDs for TermLaunch.
//
// Ranges (see CLAUDE.md): IDI_* icons, IDD_* dialogs, IDC_* controls,
// IDM_* static menu commands. Dynamic per-port commands live at
// IDM_PORT_FIRST.., dynamic speed commands at IDM_SPEED_FIRST.. — keep both
// ranges clear of anything static.

#pragma once

// ---- icons ---------------------------------------------------------------
#define IDI_APP                     101
#define IDI_PULSE1                  102
#define IDI_PULSE2                  103
#define IDI_PULSE3                  104
#define IDI_PULSE4                  105

// ---- version / strings ---------------------------------------------------
#define IDS_APP_NAME                150

// ---- dialogs -------------------------------------------------------------
#define IDD_SETTINGS                200

// ---- settings dialog controls --------------------------------------------
#define IDC_TERMINAL_PATH          1001
#define IDC_TERMINAL_BROWSE        1002
#define IDC_SESSIONS_DIR           1003
#define IDC_SESSIONS_BROWSE        1004
#define IDC_SPEED_LIST             1005
#define IDC_DEFAULT_SPEED          1006
#define IDC_RUN_ON_LOGON           1007
#define IDC_DETECT_BUSY            1008
#define IDC_ANIMATE                1009
#define IDC_SHOW_FRIENDLY          1010
#define IDC_DATA_BITS              1011
#define IDC_STOP_BITS              1012
#define IDC_PARITY                 1013
#define IDC_FLOW_CONTROL           1014
#define IDC_INI_PATH               1015
#define IDC_GRP_PATHS              1016
#define IDC_GRP_SPEEDS             1017
#define IDC_GRP_SERIAL             1018
#define IDC_GRP_BEHAVIOUR          1019
#define IDC_LBL_TERMINAL           1020
#define IDC_LBL_SESSIONS           1021
#define IDC_LBL_SPEED_LIST         1022
#define IDC_LBL_DEFAULT_SPEED      1023
#define IDC_LBL_DATA_BITS          1024
#define IDC_LBL_STOP_BITS          1025
#define IDC_LBL_PARITY             1026
#define IDC_LBL_FLOW_CONTROL       1027
#define IDC_LBL_BUSY_HINT          1028

// ---- static menu commands ------------------------------------------------
#define IDM_REFRESH                 300
#define IDM_SETTINGS                301
#define IDM_OPEN_SESSIONS           302
#define IDM_ABOUT                   303
#define IDM_EXIT                    304

// ---- dynamic menu command ranges ----------------------------------------
#define IDM_PORT_FIRST            40000
#define IDM_PORT_LAST             40999
#define IDM_SPEED_FIRST           41000
#define IDM_SPEED_LAST            41099
