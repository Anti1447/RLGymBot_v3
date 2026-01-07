; =====================================================================
; RLGymBot_v3 Render / Train Toggle Script
; 2025-12-07 - Rev 3 - Removed all ControlSend usage.
;                     All CMD interactions now use Send() only. (NJH)
; =====================================================================

#include <MsgBoxConstants.au3>
Opt("WinTitleMatchMode", 2) ; allow partial title matches

Global Const $g_sVsTitle          = "RLGymBot_v3 - ExampleMain.cpp - Microsoft Visual Studio"
Global Const $g_sCmdIdleTitle     = "x64 Native Tools Command Prompt for VS 2022"
Global Const $g_sCmdRunningTitle  = "x64 Native Tools Command Prompt for VS 2022 - GigaLearnBot.exe"
Global Const $g_sRocketSimTitle   = "RocketSimVis"

; =====================================================================
; MAIN CHOICE
; =====================================================================

Local $iChoice = MsgBox($MB_TOPMOST + $MB_ICONQUESTION + $MB_YESNOCANCEL, _
    "GigaLearn Render Toggle", _
    "Do you want to RENDER the bot and watch it play?" & @CRLF & @CRLF & _
    "YES  = Toggle ON (renderMode = true)" & @CRLF & _
    "NO   = Toggle OFF (renderMode = false)" & @CRLF & _
    "CANCEL = Exit")

Select
    Case $iChoice = $IDYES
        ToggleOn()
    Case $iChoice = $IDNO
        ToggleOff()
    Case Else
        Exit
EndSelect


; =====================================================================
; TOGGLE ON  (renderMode = true)
; =====================================================================
Func ToggleOn()

    ; --- 1) Activate Visual Studio and flip cfg.renderMode to true ----
    WinActivate($g_sVsTitle)
    WinWaitActive($g_sVsTitle, "", 10)

    ; Open Replace dialog
    Send("^h")
    Sleep(300)

    ; Replace false ? true
    Send("cfg.renderMode = false;")
    Sleep(200)
    Send("{TAB}")
    Sleep(200)
    Send("cfg.renderMode = true;")
    Sleep(200)
    Send("!a") ; Replace All
    Sleep(300)
    Send("{ESC}") ; close replace dialog
    Sleep(200)

    Send("^s") ; save
    Sleep(400)



    ; --- 2) Build the C++ program ------------------------------------
    WinActivate($g_sCmdIdleTitle)
    WinWaitActive($g_sCmdIdleTitle, "", 10)
    Sleep(150)
	Send("q")
	Send("enter")
	Sleep(3000)
    Send("cd C:\Projects\RLGymBot_v3\GigaLearnBot\build" & "{ENTER}") ; set this to the current build path
    Sleep(1000)

    Send("cmake --build ." & "{ENTER}")
    Sleep(12000)

    ; safety recompile
    Send("{UP}{ENTER}")
    Sleep(12000)

    ; run bot
    Send("GigaLearnBot.exe{ENTER}")
    Sleep(800)

    ; --- 3) Activate RocketSimVis for visualization -------------------
    WinActivate($g_sRocketSimTitle)
    WinWaitActive($g_sRocketSimTitle, "", 10)

    MsgBox($MB_TOPMOST + $MB_OK + $MB_ICONINFORMATION, _
        "Render Mode Enabled", "Ready for visual review.")

EndFunc



; =====================================================================
; TOGGLE OFF  (renderMode = false)
; =====================================================================
Func ToggleOff()

    ; --- 1) Stop the running GigaLearnBot.exe ------------------------
    If WinActivate($g_sCmdRunningTitle) Then
        WinWaitActive($g_sCmdRunningTitle, "", 5)
        Sleep(200)
        Send("^{BREAK}") ; stop immediately
        Sleep(800)
    EndIf

    ; --- 2) Flip boolean in C++ file ---------------------------------
    WinActivate($g_sVsTitle)
    WinWaitActive($g_sVsTitle, "", 10)

    Send("^h")
    Sleep(300)

    Send("cfg.renderMode = true;")
    Sleep(200)
    Send("{TAB}")
    Sleep(200)
    Send("cfg.renderMode = false;")
    Sleep(200)
    Send("!a")
    Sleep(300)
    Send("{ESC}")

    Sleep(200)
    Send("^s")
    Sleep(400)

    ; --- 3) Build C++ program headless -------------------------------
    WinActivate($g_sCmdIdleTitle)
    WinWaitActive($g_sCmdIdleTitle, "", 10)
    Sleep(150)

    Send("cd C:\Projects\RLGymBot_v3\GigaLearnBot\build" & "{ENTER}") ;set this to the current build path
    Sleep(1000)

    Send("cmake --build ." & "{ENTER}")
    Sleep(12000)

    Send("{UP}{ENTER}")
    Sleep(12000)

    Send("GigaLearnBot.exe{ENTER}")
    Sleep(800)

    MsgBox($MB_TOPMOST + $MB_OK + $MB_ICONINFORMATION, _
        "Training Mode Active", "Bot training running in progress.")

EndFunc
