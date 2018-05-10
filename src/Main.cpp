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

static void enableBetterMultiSelection();
static void showAbout();

static LRESULT CALLBACK KeyboardProc(int ncode, WPARAM wparam, LPARAM lparam);

struct Selection {
	int caret;
	int anchor;

	Selection(int caret, int anchor) : caret(caret), anchor(anchor) {}

	int start() const { return min(caret, anchor); }
	int end() const { return max(caret, anchor); }
	int length() const { return end() - start(); }
	void set(int pos) { anchor = caret = pos; }
	void offset(int offset) { anchor += offset; caret += offset; }
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

static void SetSelections(const std::vector<Selection> &selections) {
	for (size_t i = 0; i < selections.size(); ++i) {
		if (i == 0)
			editor.SetSelection(selections[i].caret, selections[i].anchor);
		else
			editor.AddSelection(selections[i].caret, selections[i].anchor);
	}
}

template<typename It>
It uniquify(It begin, It const end)
{
	std::vector<It> v;
	v.reserve(static_cast<size_t>(std::distance(begin, end)));

	for (It i = begin; i != end; ++i)
		v.push_back(i);

	std::sort(v.begin(), v.end(), [](const auto &lhs, const auto &rhs) {
		return (*lhs).start() < (*rhs).start() || (!((*rhs).start() < (*lhs).start()) && (*lhs).end() < (*rhs).end());
	});

	v.erase(std::unique(v.begin(), v.end(), [](const auto &lhs, const auto &rhs) {
		return (*lhs).start() == (*rhs).start() && (*lhs).end() == (*rhs).end();
	}), v.end());

	std::sort(v.begin(), v.end());

	size_t j = 0;
	for (It i = begin; i != end && j != v.size(); ++i) {
		if (i == v[j]) {
			using std::iter_swap; iter_swap(i, begin);
			++j;
			++begin;
		}
	}
	return begin;
}

// Create a closure that simply calls a SCI_XXX message
static auto SimpleEdit(int message) {
	return [message](Selection &selection) {
		editor.SetSelection(selection.caret, selection.anchor);
		editor.Call(message);

		selection.caret = editor.GetSelectionNCaret(0);
		selection.anchor = editor.GetSelectionNAnchor(0);
	};
}

template<typename T>
static void EditSelections(T edit) {
	auto selections = GetSelections();

	editor.ClearSelections();

	std::sort(selections.begin(), selections.end(), [](const auto &lhs, const auto &rhs) {
		return lhs.start() < rhs.start() || (!(rhs.start() < lhs.start()) && lhs.end() < rhs.end());
	});

	editor.BeginUndoAction();

	int totalOffset = 0;
	for (auto &selection : selections) {
		selection.offset(totalOffset);
		const int length = editor.GetLength();

		edit(selection);

		totalOffset += editor.GetLength() - length;
	}

	editor.EndUndoAction();

	selections.erase(uniquify(selections.begin(), selections.end()), selections.end());

	SetSelections(selections);
}

LRESULT CALLBACK KeyboardProc(int ncode, WPARAM wparam, LPARAM lparam) {
	if (ncode == HC_ACTION && (HIWORD(lparam) & KF_UP) == 0 && !IsAltPressed()) {
		if (hasFocus && editor.GetSelections() > 1) {
			if (IsControlPressed()) {
				if (wparam == VK_LEFT) {
					EditSelections(SimpleEdit(IsShiftPressed() ? SCI_WORDLEFTEXTEND : SCI_WORDLEFT));
					return TRUE; // This key has been "handled" and won't propogate
				}
				else if (wparam == VK_RIGHT) {
					EditSelections(SimpleEdit(IsShiftPressed() ? SCI_WORDRIGHTENDEXTEND : SCI_WORDRIGHT));
					return TRUE;
				}
				else if (wparam == VK_BACK) {
					EditSelections(SimpleEdit(SCI_DELWORDLEFT));
					return TRUE;
				}
				else if (wparam == VK_DELETE) {
					EditSelections(SimpleEdit(SCI_DELWORDRIGHT));
					return TRUE;
				}
			}
			else {
				if (wparam == VK_ESCAPE) {
					int caret = editor.GetSelectionNCaret(editor.GetMainSelection());
					editor.SetSelection(caret, caret);
					return TRUE;
				}
				else if (wparam == VK_LEFT) {
					EditSelections(SimpleEdit(IsShiftPressed() ? SCI_CHARLEFTEXTEND : SCI_CHARLEFT));
					return TRUE;
				}
				else if (wparam == VK_RIGHT) {
					EditSelections(SimpleEdit(IsShiftPressed() ? SCI_CHARRIGHTEXTEND : SCI_CHARRIGHT));
					return TRUE;
				}
				else if (wparam == VK_HOME) {
					EditSelections(SimpleEdit(IsShiftPressed() ? SCI_VCHOMEWRAPEXTEND : SCI_VCHOMEWRAP));
					return TRUE;
				}
				else if (wparam == VK_END) {
					EditSelections(SimpleEdit(IsShiftPressed() ? SCI_LINEENDWRAPEXTEND : SCI_LINEENDWRAP));
					return TRUE;
				}
				else if (wparam == VK_RETURN) {
					EditSelections(SimpleEdit(SCI_NEWLINE));
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
