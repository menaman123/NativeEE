#pragma once
// PS2 EE kernel constants shared by the kernel objects (thbase / thsemap /
// thevent, SDK 3.0.1). Values are the guest-visible ones: thread status bits
// (ReferThreadStatus), wait types, KE_* error codes, event-flag wait modes
// and object attributes.
#include <cstdint>

// Thread status (THS_*): ReferThreadStatus.status.
constexpr int THS_RUN = 0x01;
constexpr int THS_READY = 0x02;
constexpr int THS_WAIT = 0x04;
constexpr int THS_SUSPEND = 0x08;
constexpr int THS_WAITSUSPEND = 0x0C;
constexpr int THS_DORMANT = 0x10;

// Wait type (TSW_*): what a THS_WAIT thread waits on.
constexpr int TSW_NONE = 0;
constexpr int TSW_SLEEP = 1;
constexpr int TSW_SEMA = 2;
constexpr int TSW_EVENT = 3;

// Kernel error codes (KE_*).
constexpr int KE_OK = 0;
constexpr int KE_ERROR = -1;
constexpr int KE_ILLEGAL_PRIORITY = -403;
constexpr int KE_ILLEGAL_MODE = -405;
constexpr int KE_ILLEGAL_THID = -406;
constexpr int KE_UNKNOWN_THID = -407;
constexpr int KE_UNKNOWN_SEMID = -408;
constexpr int KE_UNKNOWN_EVFID = -409;
constexpr int KE_DORMANT = -413;
constexpr int KE_NOT_DORMANT = -414;
constexpr int KE_NOT_SUSPEND = -415;
constexpr int KE_NOT_WAIT = -416;
constexpr int KE_RELEASE_WAIT = -418;
constexpr int KE_SEMA_ZERO = -419;
constexpr int KE_SEMA_OVF = -420;
constexpr int KE_EVF_COND = -421;
constexpr int KE_EVF_MULTI = -422;
constexpr int KE_EVF_ILPAT = -423;
constexpr int KE_WAIT_DELETE = -425;

// Event-flag wait modes (WEF_*) and attributes (EA_*).
constexpr uint32_t WEF_AND = 0x00;
constexpr uint32_t WEF_OR = 0x01;
constexpr uint32_t WEF_CLEAR = 0x10;      // clear the WAITED bits on release
constexpr uint32_t WEF_CLEAR_ALL = 0x20;  // clear every bit on release
constexpr uint32_t WEF_MODE_MASK = WEF_OR | WEF_CLEAR | WEF_CLEAR_ALL;
constexpr uint32_t EA_MULTI = 0x02;       // more than one waiter allowed

// Semaphore / event-flag attribute: priority-ordered wait queue (default FIFO).
constexpr uint32_t SA_THPRI = 0x01;

// Priority range: 0 highest .. 127 lowest.
constexpr int kEeMaxPriority = 127;
