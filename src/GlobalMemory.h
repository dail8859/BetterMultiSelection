// Copyright 1998-2003 by Neil Hodgson <neilh@scintilla.org>
// The License.txt file describes the conditions under which this software may be distributed.

#ifndef GLOBALMEMORY_H
#define GLOBALMEMORY_H

#include <windows.h>

class GlobalMemory {
	HGLOBAL hand{};
public:
	void *ptr{};
	GlobalMemory() {
	}
	explicit GlobalMemory(HGLOBAL hand_) : hand(hand_) {
		if (hand) {
			ptr = ::GlobalLock(hand);
		}
	}
	// Deleted so GlobalMemory objects can not be copied.
	GlobalMemory(const GlobalMemory &) = delete;
	GlobalMemory(GlobalMemory &&) = delete;
	GlobalMemory &operator=(const GlobalMemory &) = delete;
	GlobalMemory &operator=(GlobalMemory &&) = delete;
	~GlobalMemory() {
	}
	void Allocate(size_t bytes) {
		hand = ::GlobalAlloc(GMEM_MOVEABLE | GMEM_ZEROINIT, bytes);
		if (hand) {
			ptr = ::GlobalLock(hand);
		}
	}
	HGLOBAL Unlock() {
		HGLOBAL handCopy = hand;
		::GlobalUnlock(hand);
		ptr = 0;
		hand = 0;
		return handCopy;
	}
	void SetClip(UINT uFormat) {
		::SetClipboardData(uFormat, Unlock());
	}
	operator bool() const {
		return ptr != nullptr;
	}
	SIZE_T Size() {
		return ::GlobalSize(hand);
	}
};

#endif
