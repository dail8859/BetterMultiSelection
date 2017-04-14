// This file is part of BetterMultiSelection.
// 
// Copyright (C)2017 Justin Dailey <dail8859@yahoo.com>
// 
// BetterMultiSelection is free software; you can redistribute it and/or
// modify it under the terms of the GNU General Public License
// as published by the Free Software Foundation; either
// version 2 of the License, or (at your option) any later version.
// 
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
// GNU General Public License for more details.
// 
// You should have received a copy of the GNU General Public License
// along with this program; if not, write to the Free Software
// Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.

#include "AboutDialog.h"
#include "CharClassify.h"
#include "resource.h"
#include "PluginInterface.h"
#include "ScintillaGateway.h"

#include <algorithm>
#include <vector>

#define IsShiftPressed()   ((GetKeyState(VK_SHIFT) & KF_UP) != 0)
#define IsControlPressed() ((GetKeyState(VK_CONTROL) & KF_UP) != 0)
#define IsAltPressed()     ((GetKeyState(VK_MENU) & KF_UP) != 0)


static HANDLE _hModule;
static NppData nppData;
static HHOOK hook = NULL;
static bool hasFocus = true;
static ScintillaGateway editor;
static CharClassify cc;

static void enableBetterMultiSelection();
static void showAbout();

static LRESULT CALLBACK KeyboardProc(int ncode, WPARAM wparam, LPARAM lparam);

struct Selection {
	int caret;
	int anchor;

	Selection(int caret, int anchor) : caret(caret), anchor(anchor) {}

	int start() const { return min(caret, anchor); }
	int end() const { return max(caret, anchor); }
};

static FuncItem funcItem[] = {
	{ TEXT("Enable"), enableBetterMultiSelection, 0, false, nullptr },
	{ TEXT(""), nullptr, 0, false, nullptr },
	{ TEXT("About..."), showAbout, 0, false, nullptr }
};

static const wchar_t *GetIniFilePath() {
	static wchar_t iniPath[MAX_PATH] = { 0 };

	if (iniPath[0] == 0) {
		SendMessage(nppData._nppHandle, NPPM_GETPLUGINSCONFIGDIR, MAX_PATH, (LPARAM)iniPath);
		wcscat_s(iniPath, MAX_PATH, L"\\BetterMultiSelection.ini");
	}

	return iniPath;
}

static void enableBetterMultiSelection() {
	if (hook) {
		UnhookWindowsHookEx(hook);
		hook = NULL;
		SendMessage(nppData._nppHandle, NPPM_SETMENUITEMCHECK, funcItem[0]._cmdID, 0);
	}
	else {
		hook = SetWindowsHookEx(WH_KEYBOARD, KeyboardProc, (HINSTANCE)_hModule, ::GetCurrentThreadId());
		SendMessage(nppData._nppHandle, NPPM_SETMENUITEMCHECK, funcItem[0]._cmdID, 1);
	}
}

static void showAbout() {
	ShowAboutDialog((HINSTANCE)_hModule, MAKEINTRESOURCE(IDD_ABOUTDLG), nppData._nppHandle);
}

static HWND GetCurrentScintilla() {
	int which = 0;
	SendMessage(nppData._nppHandle, NPPM_GETCURRENTSCINTILLA, SCI_UNUSED, (LPARAM)&which);
	return (which == 0) ? nppData._scintillaMainHandle : nppData._scintillaSecondHandle;
}

static std::vector<Selection> GetSelections() {
	std::vector<Selection> selections;

	int num = editor.GetSelections();
	for (int i = 0; i < num; ++i) {
		int caret = editor.GetSelectionNCaret(i);
		int anchor = editor.GetSelectionNAnchor(i);
		selections.emplace_back(Selection{ caret, anchor });
	}

	return selections;
}

static void Home(Selection &selection, bool extend) {
	selection.caret = editor.PositionFromLine(editor.LineFromPosition(selection.caret));

	if (!extend) {
		selection.anchor = selection.caret;
	}
}

static void End(Selection &selection, bool extend) {
	selection.caret = editor.GetLineEndPosition(editor.LineFromPosition(selection.caret));

	if (!extend) {
		selection.anchor = selection.caret;
	}
}

static void Left(Selection &selection, bool extend) {
	if (extend) {
		selection.caret = editor.PositionBefore(selection.caret);
	}
	else if (selection.anchor == selection.caret) {
		selection.caret = selection.anchor = editor.PositionBefore(selection.caret);
	}
	else {
		selection.caret = selection.anchor = min(selection.caret, selection.anchor);
	}
}

static void Right(Selection &selection, bool extend) {
	if (extend) {
		selection.caret = editor.PositionAfter(selection.caret);
	}
	else if (selection.anchor == selection.caret) {
		selection.anchor = selection.caret = editor.PositionAfter(selection.caret);
	}
	else {
		selection.caret = selection.anchor = max(selection.caret, selection.anchor);
	}
}

static void UpdateCharClassifier() {
	cc.SetDefaultCharClasses(true);
	cc.SetCharClasses(reinterpret_cast<const unsigned char*>(editor.GetWordChars().c_str()), CharClassify::ccWord);
	cc.SetCharClasses(reinterpret_cast<const unsigned char*>(editor.GetWhitespaceChars().c_str()), CharClassify::ccSpace);
	cc.SetCharClasses(reinterpret_cast<const unsigned char*>(editor.GetPunctuationChars().c_str()), CharClassify::ccPunctuation);
}

/**
* Find the start of the next word in either a forward (delta >= 0) or backwards direction
* (delta < 0).
* This is looking for a transition between character classes although there is also some
* additional movement to transit white space.
* Used by cursor movement by word commands.
*/
static int NextWordStart(int pos, int delta) {
	if (delta < 0) {
		while (pos > 0 && (cc.GetClass(editor.GetCharAt(editor.PositionBefore(pos))) == CharClassify::ccSpace))
			pos--;
		if (pos > 0) {
			CharClassify::cc ccStart = cc.GetClass(editor.GetCharAt(editor.PositionBefore(pos)));
			while (pos > 0 && (cc.GetClass(editor.GetCharAt(editor.PositionBefore(pos))) == ccStart)) {
				pos--;
			}
		}
	}
	else {
		const int length = editor.GetLength();
		CharClassify::cc ccStart = cc.GetClass(editor.GetCharAt(pos));
		while (pos < length && (cc.GetClass(editor.GetCharAt(pos)) == ccStart))
			pos++;
		while (pos < length && (cc.GetClass(editor.GetCharAt(pos)) == CharClassify::ccSpace))
			pos++;
	}
	return pos;
}

/**
* Find the end of the next word in either a forward (delta >= 0) or backwards direction
* (delta < 0).
* This is looking for a transition between character classes although there is also some
* additional movement to transit white space.
* Used by cursor movement by word commands.
*/
static int NextWordEnd(int pos, int delta) {
	if (delta < 0) {
		if (pos > 0) {
			CharClassify::cc ccStart = cc.GetClass(editor.GetCharAt(editor.PositionBefore(pos)));
			if (ccStart != CharClassify::ccSpace) {
				while (pos > 0 && cc.GetClass(editor.GetCharAt(editor.PositionBefore(pos))) == ccStart) {
					pos--;
				}
			}
			while (pos > 0 && cc.GetClass(editor.GetCharAt(editor.PositionBefore(pos))) == CharClassify::ccSpace) {
				pos--;
			}
		}
	}
	else {
		const int length = editor.GetLength();
		while (pos < length && cc.GetClass(editor.GetCharAt(pos)) == CharClassify::ccSpace) {
			pos++;
		}
		if (pos < length) {
			CharClassify::cc ccStart = cc.GetClass(editor.GetCharAt(pos));
			while (pos < length && cc.GetClass(editor.GetCharAt(pos)) == ccStart) {
				pos++;
			}
		}
	}
	return pos;
}

static void WordLeft(Selection &selection, bool extend) {
	selection.caret = NextWordStart(selection.caret, -1);

	if (!extend) {
		selection.anchor = selection.caret;
	}
}

static void WordRight(Selection &selection, bool extend) {
	if (extend) {
		selection.caret = NextWordEnd(selection.caret, 1);
	}
	else {
		selection.caret = NextWordStart(selection.caret, 1);
		selection.anchor = selection.caret;
	}
}

template<typename T>
static void ManipulateSelections(T manipulate, bool extend) {
	auto selections = GetSelections();

	editor.ClearSelections();

	for (auto &selection : selections)
		manipulate(selection, extend);

	// Sort the selections so only unique ones can be kept
	std::sort(selections.begin(), selections.end(), [](const auto &lhs, const auto &rhs) {
		return lhs.start() < rhs.start() || (!(rhs.start() < lhs.start()) && lhs.end() < rhs.end());
	});

	// Erase anything that directly overlaps with another one
	// Note: Partially overlapping selections are ok...SciTE also allows overlapping selections
	selections.erase(std::unique(selections.begin(), selections.end(), [](const auto &lhs, const auto &rhs) {
		return lhs.start() == rhs.start() && lhs.end() == rhs.end();
	}), selections.end());

	for (size_t i = 0; i < selections.size(); ++i) {
		if (i == 0)
			editor.SetSelection(selections[i].caret, selections[i].anchor);
		else
			editor.AddSelection(selections[i].caret, selections[i].anchor);
	}
}

LRESULT CALLBACK KeyboardProc(int ncode, WPARAM wparam, LPARAM lparam) {
	if (ncode == HC_ACTION && (HIWORD(lparam) & KF_UP) == 0 && !IsAltPressed()) {
		if (hasFocus && editor.GetSelections() > 1) {
			if (IsControlPressed()) {
				if (wparam == VK_LEFT) {
					UpdateCharClassifier();
					ManipulateSelections(WordLeft, IsShiftPressed());
					return TRUE; // This key has been "handled" and won't propogate
				}
				else if (wparam == VK_RIGHT) {
					UpdateCharClassifier();
					ManipulateSelections(WordRight, IsShiftPressed());
					return TRUE;
				}
			}
			else {
				if (wparam == VK_LEFT) {
					ManipulateSelections(Left, IsShiftPressed());
					return TRUE;
				}
				else if (wparam == VK_RIGHT) {
					ManipulateSelections(Right, IsShiftPressed());
					return TRUE;
				}
				else if (wparam == VK_HOME) {
					ManipulateSelections(Home, IsShiftPressed());
					return TRUE;
				}
				else if (wparam == VK_END) {
					ManipulateSelections(End, IsShiftPressed());
					return TRUE;
				}
			}
		}
	}
	return CallNextHookEx(hook, ncode, wparam, lparam); // pass control to next hook in the hook chain
}


BOOL APIENTRY DllMain(HANDLE hModule, DWORD  reasonForCall, LPVOID lpReserved) {
	switch (reasonForCall) {
		case DLL_PROCESS_ATTACH:
			_hModule = hModule;
			break;
		case DLL_PROCESS_DETACH:
			break;
		case DLL_THREAD_ATTACH:
			break;
		case DLL_THREAD_DETACH:
			break;
	}
	return TRUE;
}

extern "C" __declspec(dllexport) void setInfo(NppData notepadPlusData) {
	nppData = notepadPlusData;

	// Set this as early as possible so it is in a valid state
	editor.SetScintillaInstance(nppData._scintillaMainHandle);
}

extern "C" __declspec(dllexport) const wchar_t *getName() {
	return TEXT("BetterMultiSelection");
}

extern "C" __declspec(dllexport) FuncItem *getFuncsArray(int *nbF) {
	*nbF = sizeof(funcItem) / sizeof(funcItem[0]);
	return funcItem;
}

extern "C" __declspec(dllexport) void beNotified(SCNotification *notifyCode) {
	switch (notifyCode->nmhdr.code) {
		case SCN_FOCUSIN:
			hasFocus = true;
			break;
		case SCN_FOCUSOUT:
			hasFocus = false;
			break;
		case NPPN_READY: {
			bool isEnabled = GetPrivateProfileInt(TEXT("BetterMultiSelection"), TEXT("enabled"), 1, GetIniFilePath()) == 1;
			if (isEnabled) {
				hook = SetWindowsHookEx(WH_KEYBOARD, KeyboardProc, (HINSTANCE)_hModule, ::GetCurrentThreadId());
				SendMessage(nppData._nppHandle, NPPM_SETMENUITEMCHECK, funcItem[0]._cmdID, 1);
			}
			break;
		}
		case NPPN_SHUTDOWN:
			WritePrivateProfileString(TEXT("BetterMultiSelection"), TEXT("enabled"), hook ? TEXT("1") : TEXT("0"), GetIniFilePath());
			if (hook != NULL)
				UnhookWindowsHookEx(hook);
			break;
		case NPPN_BUFFERACTIVATED:
			editor.SetScintillaInstance(GetCurrentScintilla());
			break;
	}
	return;
}

extern "C" __declspec(dllexport) LRESULT messageProc(UINT Message, WPARAM wParam, LPARAM lParam) {
	return TRUE;
}

#ifdef UNICODE
extern "C" __declspec(dllexport) BOOL isUnicode() {
	return TRUE;
}
#endif
