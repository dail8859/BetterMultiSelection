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

#include "UniConversion.h"
#include "GlobalMemory.h"

#include <algorithm>
#include <vector>
#include <sstream>

#define IsShiftPressed()   ((GetKeyState(VK_SHIFT) & KF_UP) != 0)
#define IsControlPressed() ((GetKeyState(VK_CONTROL) & KF_UP) != 0)
#define IsAltPressed()     ((GetKeyState(VK_MENU) & KF_UP) != 0)


static HANDLE _hModule;
static NppData nppData;
static HHOOK hook = NULL;
static bool hasFocus = true;
static ScintillaGateway editor;

static UINT cfMultiSelect = 0;
static UINT cfColumnSelect = 0;

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
		editor.AutoCSetMulti(SC_MULTIAUTOC_EACH);
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

std::string TransformLineEnds(const char *s, int eolModeWanted) {
	std::string dest;
	const size_t len = strlen(s);
	for (size_t i = 0; s[i]; i++) {
		if (s[i] == '\n' || s[i] == '\r') {
			if (eolModeWanted == SC_EOL_CR) {
				dest.push_back('\r');
			}
			else if (eolModeWanted == SC_EOL_LF) {
				dest.push_back('\n');
			}
			else { // eolModeWanted == SC_EOL_CRLF
				dest.push_back('\r');
				dest.push_back('\n');
			}
			if ((s[i] == '\r') && (i + 1 < len) && (s[i + 1] == '\n')) {
				i++;
			}
		}
		else {
			dest.push_back(s[i]);
		}
	}
	return dest;
}

const char *StringFromEOLMode(int eolMode) {
	if (eolMode == SC_EOL_CRLF) {
		return "\r\n";
	}
	else if (eolMode == SC_EOL_CR) {
		return "\r";
	}
	else {
		return "\n";
	}
}

template <typename T, typename U>
static std::string join(const std::vector<T> &v, const U &delim) {
	std::stringstream ss;
	for (size_t i = 0; i < v.size(); ++i) {
		if (i != 0) ss << delim;
		ss << v[i];
	}
	return ss.str();
}

template <typename T>
static std::vector<std::basic_string<T>> split(std::basic_string<T> const &str, const std::basic_string<T> &delim) {
	size_t start;
	size_t end = 0;
	std::vector<std::basic_string<T>> out;

	while ((start = str.find_first_not_of(delim, end)) != std::basic_string<T>::npos) {
		end = str.find(delim, start);
		out.push_back(str.substr(start, end - start));
	}

	return out;
}

bool AllSelectionsHaveText(ScintillaGateway &editor) {
	const int selections = editor.GetSelections();
	bool has_selections = true;

	for (int i = 0; i < editor.GetSelections(); ++i) {
		int start = editor.GetSelectionNStart(i);
		int end = editor.GetSelectionNEnd(i);

		if (start == end) {
			return false;
		}
	}

	return true;
}

// ============================================================================

// OpenClipboard may fail if another application has opened the clipboard.
// Try up to 8 times, with an initial delay of 1 ms and an exponential back off
// for a maximum total delay of 127 ms (1+2+4+8+16+32+64).
bool OpenClipboardRetry(HWND hwnd) {
	for (int attempt = 0; attempt < 8; attempt++) {
		if (attempt > 0) {
			Sleep(1 << (attempt - 1));
		}
		if (OpenClipboard(hwnd)) {
			return true;
		}
	}
	return false;
}

UINT CodePageFromCharSet(DWORD characterSet, UINT documentCodePage) {
	if (documentCodePage == SC_CP_UTF8) {
		return SC_CP_UTF8;
	}
	switch (characterSet) {
	case SC_CHARSET_ANSI: return 1252;
	case SC_CHARSET_DEFAULT: return documentCodePage ? documentCodePage : 1252;
	case SC_CHARSET_BALTIC: return 1257;
	case SC_CHARSET_CHINESEBIG5: return 950;
	case SC_CHARSET_EASTEUROPE: return 1250;
	case SC_CHARSET_GB2312: return 936;
	case SC_CHARSET_GREEK: return 1253;
	case SC_CHARSET_HANGUL: return 949;
	case SC_CHARSET_MAC: return 10000;
	case SC_CHARSET_OEM: return 437;
	case SC_CHARSET_RUSSIAN: return 1251;
	case SC_CHARSET_SHIFTJIS: return 932;
	case SC_CHARSET_TURKISH: return 1254;
	case SC_CHARSET_JOHAB: return 1361;
	case SC_CHARSET_HEBREW: return 1255;
	case SC_CHARSET_ARABIC: return 1256;
	case SC_CHARSET_VIETNAMESE: return 1258;
	case SC_CHARSET_THAI: return 874;
	case SC_CHARSET_8859_15: return 28605;
		// Not supported
	case SC_CHARSET_CYRILLIC: return documentCodePage;
	case SC_CHARSET_SYMBOL: return documentCodePage;
	}
	return documentCodePage;
}


// This is a modificated version of ScintillaWin::CopyToClipboard()
// Multilpe selects can be treated like rectangular and concat'ed together by newlines
bool CopyToClipboard(ScintillaGateway &editor) {
	if (!OpenClipboardRetry(editor.GetScintillaInstance())) {
		return false;
	}

	EmptyClipboard();

	GlobalMemory uniText;

	std::string selectedText;
	for (int i = 0; i < editor.GetSelections(); ++i) {
		int start = editor.GetSelectionNStart(i);
		int end = editor.GetSelectionNEnd(i);

		editor.SetTargetRange(start, end);

		// TODO: check if newline in range and if so abort?
		// Newlines in the selection will mess up pasting since it
		// will look like an extra row

		selectedText.append(editor.GetTargetText());
		selectedText.append(StringFromEOLMode(editor.GetEOLMode()));
	}

	// Default Scintilla behaviour in Unicode mode
	if (editor.GetCodePage() == SC_CP_UTF8) {
		const size_t uchars = UTF16Length(selectedText.c_str(), selectedText.size());
		uniText.Allocate(2 * uchars);
		if (uniText) {
			UTF16FromUTF8(selectedText.c_str(), selectedText.size(), static_cast<wchar_t *>(uniText.ptr), uchars);
		}
	}
	else {
		// Not Unicode mode
		// Convert to Unicode using the current Scintilla code page
		const UINT cpSrc = CodePageFromCharSet(editor.StyleGetCharacterSet(STYLE_DEFAULT), editor.GetCodePage());
		const int uLen = MultiByteToWideChar(cpSrc, 0, selectedText.c_str(), static_cast<int>(selectedText.size()), 0, 0);
		uniText.Allocate(2 * uLen);
		if (uniText) {
			MultiByteToWideChar(cpSrc, 0, selectedText.c_str(), static_cast<int>(selectedText.size()), static_cast<wchar_t *>(uniText.ptr), uLen);
		}
	}

	if (uniText) {
		uniText.SetClip(CF_UNICODETEXT);
	}
	else {
		// There was a failure - try to copy at least ANSI text
		GlobalMemory ansiText;
		ansiText.Allocate(selectedText.size());
		if (ansiText) {
			memcpy(ansiText.ptr, selectedText.c_str(), selectedText.size());
			ansiText.SetClip(CF_TEXT);
		}
	}

	SetClipboardData(cfColumnSelect, 0);
	SetClipboardData(cfMultiSelect, 0);

	CloseClipboard();

	return true;
}

UINT CodePageOfDocument(ScintillaGateway &editor) {
	return CodePageFromCharSet(editor.StyleGetCharacterSet(STYLE_DEFAULT), editor.GetCodePage());
}

bool InsertMultiCursorPaste(ScintillaGateway &editor, const char *text) {
	std::string st;

	if (editor.GetPasteConvertEndings()) {
		st = TransformLineEnds(text, editor.GetEOLMode());
	}
	else {
		st = text;
	}

	auto lines = split(st, std::string(StringFromEOLMode(editor.GetEOLMode())));
	if (lines.size() == editor.GetSelections()) {
		EditSelections([&lines, &editor](Selection &selection) {
			if (selection.caret < selection.anchor)
				editor.SetTargetRange(selection.caret, selection.anchor);
			else
				editor.SetTargetRange(selection.anchor, selection.caret);

			editor.ReplaceTarget(lines[0]);

			selection.caret = editor.GetTargetEnd();
			selection.anchor = editor.GetTargetEnd();

			// pop front
			lines.erase(lines.cbegin());
		});

		return true;
	}

	return false;
}

bool Paste(ScintillaGateway &editor) {
	if (!IsClipboardFormatAvailable(cfColumnSelect) && !IsClipboardFormatAvailable(cfMultiSelect))
		return false;

	if (!OpenClipboardRetry(editor.GetScintillaInstance())) {
		return false;
	}

	// Always use CF_UNICODETEXT if available
	GlobalMemory memUSelection(::GetClipboardData(CF_UNICODETEXT));
	if (memUSelection) {
		const wchar_t *uptr = static_cast<const wchar_t *>(memUSelection.ptr);
		if (uptr) {
			size_t len;
			std::vector<char> putf;
			// Default Scintilla behaviour in Unicode mode
			if (editor.GetCodePage() == SC_CP_UTF8) {
				const size_t bytes = memUSelection.Size();
				len = UTF8Length(uptr, bytes / 2);
				putf.resize(len + 1);
				UTF8FromUTF16(uptr, bytes / 2, &putf[0], len);
			}
			else {
				// CF_UNICODETEXT available, but not in Unicode mode
				// Convert from Unicode to current Scintilla code page
				const UINT cpDest = CodePageOfDocument(editor);
				len = WideCharToMultiByte(cpDest, 0, uptr, -1, NULL, 0, NULL, NULL) - 1; // subtract 0 terminator
				putf.resize(len + 1);
				WideCharToMultiByte(cpDest, 0, uptr, -1, &putf[0], static_cast<int>(len) + 1, NULL, NULL);
			}

			if (InsertMultiCursorPaste(editor, &putf[0])) {
				memUSelection.Unlock();
				CloseClipboard();
				return true;
			}
		}
		
	}
	else {
		// CF_UNICODETEXT not available, paste ANSI text
		GlobalMemory memSelection(::GetClipboardData(CF_TEXT));
		if (memSelection) {
			const char *ptr = static_cast<const char *>(memSelection.ptr);
			if (ptr) {
				const size_t bytes = memSelection.Size();
				size_t len = bytes;
				for (size_t i = 0; i < bytes; i++) {
					if ((len == bytes) && (0 == ptr[i]))
						len = i;
				}

				// In Unicode mode, convert clipboard text to UTF-8
				if (editor.GetCodePage() == SC_CP_UTF8) {
					std::vector<wchar_t> uptr(len + 1);

					const int ilen = static_cast<int>(len);
					const size_t ulen = ::MultiByteToWideChar(CP_ACP, 0, ptr, ilen, &uptr[0], ilen + 1);

					const size_t mlen = UTF8Length(&uptr[0], ulen);
					std::vector<char> putf(mlen + 1);
					UTF8FromUTF16(&uptr[0], ulen, &putf[0], mlen);

					if (InsertMultiCursorPaste(editor, &putf[0])) {
						memSelection.Unlock();
						CloseClipboard();
						return true;
					}
				}
				else {
					if (InsertMultiCursorPaste(editor, ptr)) {
						memSelection.Unlock();
						CloseClipboard();
						return true;
					}
				}
			}
		}
	}

	CloseClipboard();
	return false;
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
				else if (wparam == 'X' || wparam == 'C') {
					if (CopyToClipboard(editor)) {
						if (wparam == 'X') {
							EditSelections(SimpleEdit(SCI_DELETEBACK));
						}
						return TRUE;
					}
				}
				else if (wparam == 'V') {
					if (Paste(editor)) {
						return TRUE;
					}
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
					if (!editor.AutoCActive()) {
						EditSelections(SimpleEdit(SCI_NEWLINE));
						return TRUE;
					}
					// else just let Scintilla handle the insertion of autocompletion
				}
				else if (wparam == VK_UP) {
					EditSelections(SimpleEdit(IsShiftPressed() ? SCI_LINEUPEXTEND : SCI_LINEUP));
					return TRUE;
				}
				else if (wparam == VK_DOWN) {
					EditSelections(SimpleEdit(IsShiftPressed() ? SCI_LINEDOWNEXTEND : SCI_LINEDOWN));
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
			cfColumnSelect = RegisterClipboardFormat(L"MSDEVColumnSelect");
			cfMultiSelect = RegisterClipboardFormat(L"BMSMultiSelect");
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
		case SCN_CHARADDED:
			//if (editor.GetSelections() > 1)

			break;
		case SCN_FOCUSIN:
			hasFocus = true;
			break;
		case SCN_FOCUSOUT:
			hasFocus = false;
			break;
		case NPPN_READY: {
			bool isEnabled = GetPrivateProfileInt(TEXT("BetterMultiSelection"), TEXT("enabled"), 1, GetIniFilePath()) == 1;
			if (isEnabled) {
				enableBetterMultiSelection();
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
			editor.AutoCSetMulti(SC_MULTIAUTOC_EACH);
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
