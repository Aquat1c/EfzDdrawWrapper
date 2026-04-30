/*
 * ddraw.c  –  DirectDraw 1.0 compatibility wrapper for EFZ fighting games
 *
 * Supported titles
 *   EFZ 1.11  |  EFZ BSE 2.13  |  EFZ BME 3.03 Beta  |  EFZ Memorial 4.00
 *
 * Features
 *   • Windowed mode  (default, game resolution client area, centred)
 *   • Borderless fullscreen  (F11 toggle)
 *   • Full software 8-bit palette surface emulation
 *   • GDI StretchDIBits presentation  (no D3D / DXGI dependency)
 *   • Per-frame logging to ddraw_wrapper.log next to the EXE
 *
 * Build: 32-bit only (game executables are PE32 i386)
 *        See CMakeLists.txt or build.bat
 */

#define WIN32_LEAN_AND_MEAN
#define _CRT_SECURE_NO_WARNINGS
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>

#ifndef DDRAW_WRAPPER_ENABLE_LOGGING
#define DDRAW_WRAPPER_ENABLE_LOGGING 1
#endif

/* ═══════════════════════════════════════════════════════════════════════════
   HRESULT / error codes
   ═══════════════════════════════════════════════════════════════════════════ */
#define DD_OK                      0x00000000L
#define DDERR_GENERIC              0x80004005L
#define DDERR_UNSUPPORTED          0x80004001L
#define DDERR_INVALIDPARAMS        0x80070057L
#define DDERR_OUTOFMEMORY          0x8007000EL
#define DDERR_SURFACELOST          0x887601C2L
#define DDERR_WASSTILLDRAWING      0x887601C4L
#define DDERR_NOTLOCKED            0x887601C8L
#define DDERR_ALREADYLOCKED        0x887601CAL

/* ═══════════════════════════════════════════════════════════════════════════
   DDSCAPS flags
   ═══════════════════════════════════════════════════════════════════════════ */
#define DDSCAPS_BACKBUFFER         0x00000004
#define DDSCAPS_COMPLEX            0x00000008
#define DDSCAPS_FLIP               0x00000010
#define DDSCAPS_OFFSCREENPLAIN     0x00000040
#define DDSCAPS_PRIMARYSURFACE     0x00000200
#define DDSCAPS_SYSTEMMEMORY       0x00000800
#define DDSCAPS_VIDEOMEMORY        0x00004000

/* ═══════════════════════════════════════════════════════════════════════════
   DDSD flags
   ═══════════════════════════════════════════════════════════════════════════ */
#define DDSD_CAPS                  0x00000001
#define DDSD_HEIGHT                0x00000002
#define DDSD_WIDTH                 0x00000004
#define DDSD_PITCH                 0x00000008
#define DDSD_BACKBUFFERCOUNT       0x00000020
#define DDSD_LPSURFACE             0x00000800
#define DDSD_PIXELFORMAT           0x00001000

/* ═══════════════════════════════════════════════════════════════════════════
   DDLOCK flags
   ═══════════════════════════════════════════════════════════════════════════ */
#define DDLOCK_WAIT                0x00000001
#define DDLOCK_READONLY            0x00000010
#define DDLOCK_WRITEONLY           0x00000020
#define DDLOCK_NOSYSLOCK           0x00000800

/* ═══════════════════════════════════════════════════════════════════════════
   DDSCL flags (SetCooperativeLevel)
   ═══════════════════════════════════════════════════════════════════════════ */
#define DDSCL_FULLSCREEN           0x00000001
#define DDSCL_ALLOWMODEX           0x00000004
#define DDSCL_NORMAL               0x00000008
#define DDSCL_EXCLUSIVE            0x00000010
#define DDSCL_ALLOWREBOOT          0x00000002

/* ═══════════════════════════════════════════════════════════════════════════
   DDPCAPS flags (CreatePalette)
   ═══════════════════════════════════════════════════════════════════════════ */
#define DDPCAPS_8BIT               0x00000004
#define DDPCAPS_ALLOW256           0x00000040

/* ═══════════════════════════════════════════════════════════════════════════
   DDBLTFAST flags (BltFast)
   ═══════════════════════════════════════════════════════════════════════════ */
#define DDBLTFAST_NOCOLORKEY       0x00000000
#define DDBLTFAST_SRCCOLORKEY      0x00000001
#define DDBLTFAST_DESTCOLORKEY     0x00000002
#define DDBLTFAST_WAIT             0x00000010

/* ═══════════════════════════════════════════════════════════════════════════
   DDBLT flags (Blt)
   ═══════════════════════════════════════════════════════════════════════════ */
#define DDBLT_COLORFILL            0x00000400
#define DDBLT_KEYSRC               0x00008000
#define DDBLT_KEYDEST              0x00004000
#define DDBLT_WAIT                 0x01000000

/* ═══════════════════════════════════════════════════════════════════════════
   DDCKEY flags (SetColorKey)
   ═══════════════════════════════════════════════════════════════════════════ */
#define DDCKEY_COLORSPACE          0x00000001
#define DDCKEY_DESTBLT             0x00000002
#define DDCKEY_DESTOVERLAY         0x00000004
#define DDCKEY_SRCBLT              0x00000008
#define DDCKEY_SRCOVERLAY          0x00000010

/* ═══════════════════════════════════════════════════════════════════════════
   DDSURFACEDESC  –  manual layout, exactly 108 bytes on 32-bit
   Must match the Windows SDK struct byte-for-byte so the game's stack
   variables (e.g. v15[0..26]) map correctly.
   ═══════════════════════════════════════════════════════════════════════════ */
#pragma pack(push, 4)

typedef struct { DWORD dwLow; DWORD dwHigh; } MY_DDCOLORKEY;   /* 8 bytes */

typedef struct {          /* 32 bytes, matches DDPIXELFORMAT */
    DWORD dwSize;         /* offset  0 */
    DWORD dwFlags;        /* offset  4 */
    DWORD dwFourCC;       /* offset  8 */
    DWORD dwRGBBitCount;  /* offset 12 */
    DWORD dwRBitMask;     /* offset 16 */
    DWORD dwGBitMask;     /* offset 20 */
    DWORD dwBBitMask;     /* offset 24 */
    DWORD dwRGBAlphaBitMask; /* offset 28 */
} MY_DDPIXELFORMAT;

typedef struct { DWORD dwCaps; } MY_DDSCAPS;   /* 4 bytes */

typedef struct {                      /* total = 108 bytes */
    DWORD           dwSize;           /* offset   0 */
    DWORD           dwFlags;          /* offset   4 */
    DWORD           dwHeight;         /* offset   8 */
    DWORD           dwWidth;          /* offset  12 */
    LONG            lPitch;           /* offset  16 */
    DWORD           dwBackBufferCount;/* offset  20 */
    DWORD           dwMipMapCount;    /* offset  24 */
    DWORD           dwAlphaBitDepth;  /* offset  28 */
    DWORD           dwReserved;       /* offset  32 */
    DWORD           lpSurface;        /* offset  36  (DWORD keeps size stable on 32-bit) */
    MY_DDCOLORKEY   ddckCKDestOverlay;/* offset  40 */
    MY_DDCOLORKEY   ddckCKDestBlt;    /* offset  48 */
    MY_DDCOLORKEY   ddckCKSrcOverlay; /* offset  56 */
    MY_DDCOLORKEY   ddckCKSrcBlt;     /* offset  64 */
    MY_DDPIXELFORMAT ddpfPixelFormat; /* offset  72 */
    MY_DDSCAPS      ddsCaps;          /* offset 104 */
} MY_DDSURFACEDESC;                   /* = 108 bytes */

#pragma pack(pop)

/* compile-time size guard */
typedef char _ASSERT_DDSURFACEDESC_SIZE[(sizeof(MY_DDSURFACEDESC) == 108) ? 1 : -1];

/* ═══════════════════════════════════════════════════════════════════════════
   COM object forward declarations
   ═══════════════════════════════════════════════════════════════════════════ */
typedef struct MyDDraw     MyDDraw;
typedef struct MyDDSurface MyDDSurface;
typedef struct MyDDPalette MyDDPalette;

/* ═══════════════════════════════════════════════════════════════════════════
   COM object definitions
   ═══════════════════════════════════════════════════════════════════════════ */
struct MyDDPalette {
    void   *lpVtbl;
    LONG    refCount;
    RGBQUAD entries[256];   /* pre-converted from PALETTEENTRY (BGR→RGB swap) */
};

struct MyDDSurface {
    void        *lpVtbl;
    LONG         refCount;
    int          width, height, pitch;  /* pitch = width rounded up to 4-byte boundary */
    BYTE        *pixels;
    BOOL         isPrimary;
    BOOL         isBackBuffer;
    MyDDSurface *attachedBackBuffer;    /* primary → back-buffer link  */
    MyDDPalette *palette;
    MyDDraw     *owner;
    BOOL         locked;
    BOOL         hasColorKeySrc;
    DWORD        colorKeySrcLow, colorKeySrcHigh;
};

struct MyDDraw {
    void       *lpVtbl;
    LONG        refCount;
    HWND        hWnd;
    WNDPROC     origWndProc;
    int         gameWidth, gameHeight;
    BOOL        borderless;
    RECT        savedWinRect;
    LONG        savedStyle, savedExStyle;
    MyDDSurface *primarySurface;
};

/* ═══════════════════════════════════════════════════════════════════════════
   Global state
   ═══════════════════════════════════════════════════════════════════════════ */
static MyDDraw         *g_dd        = NULL;
static FILE            *g_log       = NULL;
static CRITICAL_SECTION g_logCS;
static BOOL             g_logCSInit = FALSE;
static HINSTANCE        g_hInst     = NULL;
static HHOOK            g_kbHook    = NULL;
static BOOL             g_appActive = TRUE;
static BOOL             g_dinputHooked = FALSE;

#if DDRAW_WRAPPER_ENABLE_LOGGING
static void LOG(const char *fmt, ...);
#else
#define LOG(...) ((void)0)
#endif

/* ═══════════════════════════════════════════════════════════════════════════
   DirectInput guard

   EFZ keeps polling foreground DirectInput devices after our DirectDraw shim
   has made the game windowed. When focus is lost, GetDeviceState can fail
   without filling the caller's buffer; the game then reads stack/stale bytes
   as DIK_ESCAPE/DIK_F1...DIK_F12. We wrap the devices and turn lost/inactive
   foreground input into a clean "nothing pressed" state.
   ═══════════════════════════════════════════════════════════════════════════ */
#define DIERR_INPUTLOST          0x8007001EL
#define DIERR_NOTACQUIRED        0x8007000CL
#define DISCL_EXCLUSIVE_DI       0x00000001
#define DISCL_NONEXCLUSIVE_DI    0x00000002
#define DISCL_FOREGROUND_DI      0x00000004
#define DISCL_NOWINKEY_DI        0x00000010

typedef HRESULT (WINAPI *PFN_DirectInputCreateEx)(
    HINSTANCE hinst, DWORD dwVersion, REFIID riidltf, void **ppvOut,
    void *punkOuter);
typedef HRESULT (WINAPI *PFN_DirectInputCreateA)(
    HINSTANCE hinst, DWORD dwVersion, void **ppvOut, void *punkOuter);

typedef struct DInputProxy DInputProxy;
typedef struct DInputDeviceProxy DInputDeviceProxy;

struct DInputProxy {
    void *lpVtbl;
    LONG  refCount;
    void *real;
};

struct DInputDeviceProxy {
    void *lpVtbl;
    LONG  refCount;
    void *real;
    DWORD coopFlags;
    HWND  hWnd;
    DWORD stateCalls;
    DWORD stateFailures;
    DWORD stateInactiveSuppressions;
    DWORD pollCalls;
    DWORD pollFailures;
    DWORD pollInactiveSuppressions;
};

static PFN_DirectInputCreateEx g_realDirectInputCreateEx = NULL;
static PFN_DirectInputCreateA  g_realDirectInputCreateA = NULL;
static DWORD g_focusSerial = 0;
static DWORD g_lastFocusTick = 0;
static DWORD g_presentSerial = 0;
static DWORD g_flipSerial = 0;
static BOOL  g_renderTraceAfterFocus = FALSE;
static int   g_renderTraceBudget = 0;
static int   g_wndTraceBudget = 0;

static HRESULT WINAPI Hook_DirectInputCreateEx(
    HINSTANCE hinst, DWORD dwVersion, REFIID riidltf, void **ppvOut,
    void *punkOuter);
static HRESULT WINAPI Hook_DirectInputCreateA(
    HINSTANCE hinst, DWORD dwVersion, void **ppvOut, void *punkOuter);

static BOOL hr_failed(HRESULT hr)
{
    return hr < 0;
}

static BOOL should_log_counter(DWORD count)
{
    return count <= 8 || (count % 120) == 0;
}

static HRESULT call_real_release(void *real)
{
    typedef HRESULT (__stdcall *Fn)(void *);
    return ((Fn)(*(void ***)real)[2])(real);
}

static DInputDeviceProxy *wrap_dinput_device(void *real)
{
    static void *deviceVtbl[32];
    DInputDeviceProxy *dev;

    if (!real) return NULL;

    dev = (DInputDeviceProxy *)calloc(1, sizeof(*dev));
    if (!dev) return NULL;

    dev->lpVtbl = deviceVtbl;
    dev->refCount = 1;
    dev->real = real;
    dev->coopFlags = DISCL_FOREGROUND_DI;
    return dev;
}

static HRESULT __stdcall DIDev_QueryInterface(DInputDeviceProxy *dev,
    REFIID riid, void **ppv)
{
    if (!ppv) return DDERR_INVALIDPARAMS;
    *ppv = dev;
    InterlockedIncrement(&dev->refCount);
    return DD_OK;
}

static ULONG __stdcall DIDev_AddRef(DInputDeviceProxy *dev)
{
    return (ULONG)InterlockedIncrement(&dev->refCount);
}

static ULONG __stdcall DIDev_Release(DInputDeviceProxy *dev)
{
    LONG ref = InterlockedDecrement(&dev->refCount);
    if (ref == 0) {
        call_real_release(dev->real);
        free(dev);
    }
    return (ULONG)ref;
}

static HRESULT __stdcall DIDev_GetCapabilities(DInputDeviceProxy *dev, void *caps)
{
    typedef HRESULT (__stdcall *Fn)(void *, void *);
    return ((Fn)(*(void ***)dev->real)[3])(dev->real, caps);
}

static HRESULT __stdcall DIDev_EnumObjects(DInputDeviceProxy *dev,
    void *callback, void *ref, DWORD flags)
{
    typedef HRESULT (__stdcall *Fn)(void *, void *, void *, DWORD);
    return ((Fn)(*(void ***)dev->real)[4])(dev->real, callback, ref, flags);
}

static HRESULT __stdcall DIDev_GetProperty(DInputDeviceProxy *dev,
    REFGUID prop, void *header)
{
    typedef HRESULT (__stdcall *Fn)(void *, REFGUID, void *);
    return ((Fn)(*(void ***)dev->real)[5])(dev->real, prop, header);
}

static HRESULT __stdcall DIDev_SetProperty(DInputDeviceProxy *dev,
    REFGUID prop, void *header)
{
    typedef HRESULT (__stdcall *Fn)(void *, REFGUID, void *);
    return ((Fn)(*(void ***)dev->real)[6])(dev->real, prop, header);
}

static HRESULT __stdcall DIDev_Acquire(DInputDeviceProxy *dev)
{
    typedef HRESULT (__stdcall *Fn)(void *);
    HRESULT hr = ((Fn)(*(void ***)dev->real)[7])(dev->real);
    LOG("DI Acquire dev=%p real=%p hwnd=%p flags=%08X active=%d hr=%08X",
        (void *)dev, dev->real, (void *)dev->hWnd, dev->coopFlags,
        g_appActive, (DWORD)hr);
    return hr;
}

static HRESULT __stdcall DIDev_Unacquire(DInputDeviceProxy *dev)
{
    typedef HRESULT (__stdcall *Fn)(void *);
    HRESULT hr = ((Fn)(*(void ***)dev->real)[8])(dev->real);
    LOG("DI Unacquire dev=%p real=%p hwnd=%p flags=%08X active=%d hr=%08X",
        (void *)dev, dev->real, (void *)dev->hWnd, dev->coopFlags,
        g_appActive, (DWORD)hr);
    return hr;
}

static HRESULT __stdcall DIDev_GetDeviceState(DInputDeviceProxy *dev,
    DWORD cbData, void *data)
{
    typedef HRESULT (__stdcall *GetStateFn)(void *, DWORD, void *);
    typedef HRESULT (__stdcall *AcquireFn)(void *);
    HRESULT hr;

    if (!data) {
        return ((GetStateFn)(*(void ***)dev->real)[9])(dev->real, cbData, data);
    }

    dev->stateCalls++;
    if ((dev->coopFlags & DISCL_FOREGROUND_DI) && !g_appActive) {
        ZeroMemory(data, cbData);
        dev->stateInactiveSuppressions++;
        if (should_log_counter(dev->stateInactiveSuppressions)) {
            LOG("DI GetDeviceState inactive-zero dev=%p call=%u inactiveZero=%u cb=%u hwnd=%p fg=%p focusSerial=%u",
                (void *)dev, dev->stateCalls, dev->stateInactiveSuppressions,
                cbData, (void *)dev->hWnd, (void *)GetForegroundWindow(),
                g_focusSerial);
        }
        return DD_OK;
    }

    hr = ((GetStateFn)(*(void ***)dev->real)[9])(dev->real, cbData, data);
    if (hr_failed(hr)) {
        HRESULT acquireHr;
        ZeroMemory(data, cbData);
        dev->stateFailures++;
        acquireHr = ((AcquireFn)(*(void ***)dev->real)[7])(dev->real);
        if (should_log_counter(dev->stateFailures)) {
            LOG("DI GetDeviceState fail-zero dev=%p call=%u fail=%u cb=%u hr=%08X acquireHr=%08X active=%d hwnd=%p fg=%p focusSerial=%u",
                (void *)dev, dev->stateCalls, dev->stateFailures, cbData,
                (DWORD)hr, (DWORD)acquireHr, g_appActive, (void *)dev->hWnd,
                (void *)GetForegroundWindow(), g_focusSerial);
        }
        return DD_OK;
    }

    if (dev->stateCalls <= 8 || g_renderTraceAfterFocus) {
        LOG("DI GetDeviceState ok dev=%p call=%u cb=%u active=%d hwnd=%p focusSerial=%u",
            (void *)dev, dev->stateCalls, cbData, g_appActive,
            (void *)dev->hWnd, g_focusSerial);
    }
    return hr;
}

static HRESULT __stdcall DIDev_GetDeviceData(DInputDeviceProxy *dev,
    DWORD cbObjectData, void *objectData, DWORD *inOut, DWORD flags)
{
    typedef HRESULT (__stdcall *Fn)(void *, DWORD, void *, DWORD *, DWORD);
    return ((Fn)(*(void ***)dev->real)[10])(dev->real, cbObjectData, objectData,
                                          inOut, flags);
}

static HRESULT __stdcall DIDev_SetDataFormat(DInputDeviceProxy *dev, void *fmt)
{
    typedef HRESULT (__stdcall *Fn)(void *, void *);
    return ((Fn)(*(void ***)dev->real)[11])(dev->real, fmt);
}

static HRESULT __stdcall DIDev_SetEventNotification(DInputDeviceProxy *dev,
    HANDLE event)
{
    typedef HRESULT (__stdcall *Fn)(void *, HANDLE);
    return ((Fn)(*(void ***)dev->real)[12])(dev->real, event);
}

static HRESULT __stdcall DIDev_SetCooperativeLevel(DInputDeviceProxy *dev,
    HWND hWnd, DWORD flags)
{
    typedef HRESULT (__stdcall *Fn)(void *, HWND, DWORD);
    HRESULT hr;
    DWORD origFlags = flags;
    dev->hWnd = hWnd;

    /* DISCL_EXCLUSIVE for keyboard is deprecated on Vista+ but may still route
     * input exclusively and block RegisterHotKey / other LL hooks (OBS etc.).
     * DISCL_NOWINKEY is still honored on Windows 10 and suppresses the Win key.
     * Force DISCL_NONEXCLUSIVE and strip DISCL_NOWINKEY on all devices so that
     * system shortcuts (Alt+Tab, Win key, OBS hotkeys) continue to work. */
    flags = (flags & ~(DISCL_EXCLUSIVE_DI | DISCL_NOWINKEY_DI)) | DISCL_NONEXCLUSIVE_DI;

    dev->coopFlags = flags;
    hr = ((Fn)(*(void ***)dev->real)[13])(dev->real, hWnd, flags);
    LOG("DI SetCooperativeLevel dev=%p real=%p hwnd=%p origFlags=%08X flags=%08X active=%d hr=%08X",
        (void *)dev, dev->real, (void *)hWnd, origFlags, flags, g_appActive, (DWORD)hr);
    return hr;
}

static HRESULT __stdcall DIDev_GetObjectInfo(DInputDeviceProxy *dev,
    void *objectInfo, DWORD object, DWORD how)
{
    typedef HRESULT (__stdcall *Fn)(void *, void *, DWORD, DWORD);
    return ((Fn)(*(void ***)dev->real)[14])(dev->real, objectInfo, object, how);
}

static HRESULT __stdcall DIDev_GetDeviceInfo(DInputDeviceProxy *dev,
    void *deviceInfo)
{
    typedef HRESULT (__stdcall *Fn)(void *, void *);
    return ((Fn)(*(void ***)dev->real)[15])(dev->real, deviceInfo);
}

static HRESULT __stdcall DIDev_RunControlPanel(DInputDeviceProxy *dev,
    HWND owner, DWORD flags)
{
    typedef HRESULT (__stdcall *Fn)(void *, HWND, DWORD);
    return ((Fn)(*(void ***)dev->real)[16])(dev->real, owner, flags);
}

static HRESULT __stdcall DIDev_Initialize(DInputDeviceProxy *dev,
    HINSTANCE hinst, DWORD version, REFGUID guid)
{
    typedef HRESULT (__stdcall *Fn)(void *, HINSTANCE, DWORD, REFGUID);
    return ((Fn)(*(void ***)dev->real)[17])(dev->real, hinst, version, guid);
}

static HRESULT __stdcall DIDev_CreateEffect(DInputDeviceProxy *dev,
    REFGUID guid, void *effect, void **out, void *outer)
{
    typedef HRESULT (__stdcall *Fn)(void *, REFGUID, void *, void **, void *);
    return ((Fn)(*(void ***)dev->real)[18])(dev->real, guid, effect, out, outer);
}

static HRESULT __stdcall DIDev_EnumEffects(DInputDeviceProxy *dev,
    void *callback, void *ref, DWORD type)
{
    typedef HRESULT (__stdcall *Fn)(void *, void *, void *, DWORD);
    return ((Fn)(*(void ***)dev->real)[19])(dev->real, callback, ref, type);
}

static HRESULT __stdcall DIDev_GetEffectInfo(DInputDeviceProxy *dev,
    void *effectInfo, REFGUID guid)
{
    typedef HRESULT (__stdcall *Fn)(void *, void *, REFGUID);
    return ((Fn)(*(void ***)dev->real)[20])(dev->real, effectInfo, guid);
}

static HRESULT __stdcall DIDev_GetForceFeedbackState(DInputDeviceProxy *dev,
    DWORD *state)
{
    typedef HRESULT (__stdcall *Fn)(void *, DWORD *);
    return ((Fn)(*(void ***)dev->real)[21])(dev->real, state);
}

static HRESULT __stdcall DIDev_SendForceFeedbackCommand(DInputDeviceProxy *dev,
    DWORD flags)
{
    typedef HRESULT (__stdcall *Fn)(void *, DWORD);
    return ((Fn)(*(void ***)dev->real)[22])(dev->real, flags);
}

static HRESULT __stdcall DIDev_EnumCreatedEffectObjects(DInputDeviceProxy *dev,
    void *callback, void *ref, DWORD flags)
{
    typedef HRESULT (__stdcall *Fn)(void *, void *, void *, DWORD);
    return ((Fn)(*(void ***)dev->real)[23])(dev->real, callback, ref, flags);
}

static HRESULT __stdcall DIDev_Escape(DInputDeviceProxy *dev, void *escape)
{
    typedef HRESULT (__stdcall *Fn)(void *, void *);
    return ((Fn)(*(void ***)dev->real)[24])(dev->real, escape);
}

static HRESULT __stdcall DIDev_Poll(DInputDeviceProxy *dev)
{
    typedef HRESULT (__stdcall *Fn)(void *);
    HRESULT hr;

    dev->pollCalls++;
    if ((dev->coopFlags & DISCL_FOREGROUND_DI) && !g_appActive) {
        dev->pollInactiveSuppressions++;
        if (should_log_counter(dev->pollInactiveSuppressions)) {
            LOG("DI Poll inactive-ok dev=%p call=%u inactivePoll=%u hwnd=%p fg=%p focusSerial=%u",
                (void *)dev, dev->pollCalls, dev->pollInactiveSuppressions,
                (void *)dev->hWnd, (void *)GetForegroundWindow(),
                g_focusSerial);
        }
        return DD_OK;
    }

    hr = ((Fn)(*(void ***)dev->real)[25])(dev->real);
    if (hr_failed(hr)) {
        dev->pollFailures++;
        if (should_log_counter(dev->pollFailures)) {
            LOG("DI Poll fail-masked dev=%p call=%u fail=%u hr=%08X active=%d hwnd=%p fg=%p focusSerial=%u",
                (void *)dev, dev->pollCalls, dev->pollFailures, (DWORD)hr,
                g_appActive, (void *)dev->hWnd, (void *)GetForegroundWindow(),
                g_focusSerial);
        }
        return DD_OK;
    }
    if (dev->pollCalls <= 8 || g_renderTraceAfterFocus) {
        LOG("DI Poll ok dev=%p call=%u active=%d hwnd=%p focusSerial=%u",
            (void *)dev, dev->pollCalls, g_appActive, (void *)dev->hWnd,
            g_focusSerial);
    }
    return hr;
}

static HRESULT __stdcall DIDev_SendDeviceData(DInputDeviceProxy *dev,
    DWORD cbObjectData, void *objectData, DWORD *inOut, DWORD flags)
{
    typedef HRESULT (__stdcall *Fn)(void *, DWORD, void *, DWORD *, DWORD);
    return ((Fn)(*(void ***)dev->real)[26])(dev->real, cbObjectData, objectData,
                                          inOut, flags);
}

static HRESULT __stdcall DIDev_EnumEffectsInFile(DInputDeviceProxy *dev,
    const char *fileName, void *callback, void *ref, DWORD flags)
{
    typedef HRESULT (__stdcall *Fn)(void *, const char *, void *, void *, DWORD);
    return ((Fn)(*(void ***)dev->real)[27])(dev->real, fileName, callback, ref,
                                          flags);
}

static HRESULT __stdcall DIDev_WriteEffectToFile(DInputDeviceProxy *dev,
    const char *fileName, DWORD entries, void *fileEffects, DWORD flags)
{
    typedef HRESULT (__stdcall *Fn)(void *, const char *, DWORD, void *, DWORD);
    return ((Fn)(*(void ***)dev->real)[28])(dev->real, fileName, entries,
                                          fileEffects, flags);
}

static HRESULT __stdcall DIDev_BuildActionMap(DInputDeviceProxy *dev,
    void *actionFormat, const char *userName, DWORD flags)
{
    typedef HRESULT (__stdcall *Fn)(void *, void *, const char *, DWORD);
    return ((Fn)(*(void ***)dev->real)[29])(dev->real, actionFormat, userName,
                                          flags);
}

static HRESULT __stdcall DIDev_SetActionMap(DInputDeviceProxy *dev,
    void *actionFormat, const char *userName, DWORD flags)
{
    typedef HRESULT (__stdcall *Fn)(void *, void *, const char *, DWORD);
    return ((Fn)(*(void ***)dev->real)[30])(dev->real, actionFormat, userName,
                                          flags);
}

static HRESULT __stdcall DIDev_GetImageInfo(DInputDeviceProxy *dev,
    void *imageInfo)
{
    typedef HRESULT (__stdcall *Fn)(void *, void *);
    return ((Fn)(*(void ***)dev->real)[31])(dev->real, imageInfo);
}

static void init_dinput_device_vtbl(void)
{
    DInputDeviceProxy *dummy = NULL;
    void **vtbl;

    dummy = wrap_dinput_device((void *)1);
    if (!dummy) return;
    vtbl = (void **)dummy->lpVtbl;
    vtbl[0]  = DIDev_QueryInterface;
    vtbl[1]  = DIDev_AddRef;
    vtbl[2]  = DIDev_Release;
    vtbl[3]  = DIDev_GetCapabilities;
    vtbl[4]  = DIDev_EnumObjects;
    vtbl[5]  = DIDev_GetProperty;
    vtbl[6]  = DIDev_SetProperty;
    vtbl[7]  = DIDev_Acquire;
    vtbl[8]  = DIDev_Unacquire;
    vtbl[9]  = DIDev_GetDeviceState;
    vtbl[10] = DIDev_GetDeviceData;
    vtbl[11] = DIDev_SetDataFormat;
    vtbl[12] = DIDev_SetEventNotification;
    vtbl[13] = DIDev_SetCooperativeLevel;
    vtbl[14] = DIDev_GetObjectInfo;
    vtbl[15] = DIDev_GetDeviceInfo;
    vtbl[16] = DIDev_RunControlPanel;
    vtbl[17] = DIDev_Initialize;
    vtbl[18] = DIDev_CreateEffect;
    vtbl[19] = DIDev_EnumEffects;
    vtbl[20] = DIDev_GetEffectInfo;
    vtbl[21] = DIDev_GetForceFeedbackState;
    vtbl[22] = DIDev_SendForceFeedbackCommand;
    vtbl[23] = DIDev_EnumCreatedEffectObjects;
    vtbl[24] = DIDev_Escape;
    vtbl[25] = DIDev_Poll;
    vtbl[26] = DIDev_SendDeviceData;
    vtbl[27] = DIDev_EnumEffectsInFile;
    vtbl[28] = DIDev_WriteEffectToFile;
    vtbl[29] = DIDev_BuildActionMap;
    vtbl[30] = DIDev_SetActionMap;
    vtbl[31] = DIDev_GetImageInfo;
    free(dummy);
}

static void *wrap_dinput_device_out(void *real)
{
    static BOOL initialized = FALSE;
    DInputDeviceProxy *dev;

    if (!initialized) {
        init_dinput_device_vtbl();
        initialized = TRUE;
    }

    dev = wrap_dinput_device(real);
    if (!dev) return real;
    LOG("DirectInput device wrapped real=%p proxy=%p", real, (void *)dev);
    return dev;
}

static HRESULT __stdcall DI_QueryInterface(DInputProxy *di,
    REFIID riid, void **ppv)
{
    if (!ppv) return DDERR_INVALIDPARAMS;
    *ppv = di;
    InterlockedIncrement(&di->refCount);
    return DD_OK;
}

static ULONG __stdcall DI_AddRef(DInputProxy *di)
{
    return (ULONG)InterlockedIncrement(&di->refCount);
}

static ULONG __stdcall DI_Release(DInputProxy *di)
{
    LONG ref = InterlockedDecrement(&di->refCount);
    if (ref == 0) {
        call_real_release(di->real);
        free(di);
    }
    return (ULONG)ref;
}

static HRESULT __stdcall DI_CreateDevice(DInputProxy *di,
    REFGUID guid, void **deviceOut, void *outer)
{
    typedef HRESULT (__stdcall *Fn)(void *, REFGUID, void **, void *);
    HRESULT hr = ((Fn)(*(void ***)di->real)[3])(di->real, guid, deviceOut, outer);
    if (!hr_failed(hr) && deviceOut && *deviceOut)
        *deviceOut = wrap_dinput_device_out(*deviceOut);
    return hr;
}

static HRESULT __stdcall DI_EnumDevices(DInputProxy *di,
    DWORD devType, void *callback, void *ref, DWORD flags)
{
    typedef HRESULT (__stdcall *Fn)(void *, DWORD, void *, void *, DWORD);
    return ((Fn)(*(void ***)di->real)[4])(di->real, devType, callback, ref, flags);
}

static HRESULT __stdcall DI_GetDeviceStatus(DInputProxy *di, REFGUID guid)
{
    typedef HRESULT (__stdcall *Fn)(void *, REFGUID);
    return ((Fn)(*(void ***)di->real)[5])(di->real, guid);
}

static HRESULT __stdcall DI_RunControlPanel(DInputProxy *di,
    HWND owner, DWORD flags)
{
    typedef HRESULT (__stdcall *Fn)(void *, HWND, DWORD);
    return ((Fn)(*(void ***)di->real)[6])(di->real, owner, flags);
}

static HRESULT __stdcall DI_Initialize(DInputProxy *di,
    HINSTANCE hinst, DWORD version)
{
    typedef HRESULT (__stdcall *Fn)(void *, HINSTANCE, DWORD);
    return ((Fn)(*(void ***)di->real)[7])(di->real, hinst, version);
}

static HRESULT __stdcall DI_FindDevice(DInputProxy *di,
    REFGUID guid, const char *name, GUID *outGuid)
{
    typedef HRESULT (__stdcall *Fn)(void *, REFGUID, const char *, GUID *);
    return ((Fn)(*(void ***)di->real)[8])(di->real, guid, name, outGuid);
}

static HRESULT __stdcall DI_CreateDeviceEx(DInputProxy *di,
    REFGUID guid, REFIID riid, void **deviceOut, void *outer)
{
    typedef HRESULT (__stdcall *Fn)(void *, REFGUID, REFIID, void **, void *);
    HRESULT hr = ((Fn)(*(void ***)di->real)[9])(di->real, guid, riid,
                                              deviceOut, outer);
    if (!hr_failed(hr) && deviceOut && *deviceOut)
        *deviceOut = wrap_dinput_device_out(*deviceOut);
    return hr;
}

static void *wrap_dinput(void *real)
{
    static void *diVtbl[10] = {
        DI_QueryInterface,
        DI_AddRef,
        DI_Release,
        DI_CreateDevice,
        DI_EnumDevices,
        DI_GetDeviceStatus,
        DI_RunControlPanel,
        DI_Initialize,
        DI_FindDevice,
        DI_CreateDeviceEx
    };
    DInputProxy *di;

    if (!real) return NULL;
    di = (DInputProxy *)calloc(1, sizeof(*di));
    if (!di) return real;

    di->lpVtbl = diVtbl;
    di->refCount = 1;
    di->real = real;
    LOG("DirectInput wrapped real=%p proxy=%p", real, (void *)di);
    return di;
}

static HRESULT WINAPI Hook_DirectInputCreateEx(
    HINSTANCE hinst, DWORD dwVersion, REFIID riidltf, void **ppvOut,
    void *punkOuter)
{
    HRESULT hr;

    if (!g_realDirectInputCreateEx)
        return DDERR_GENERIC;

    hr = g_realDirectInputCreateEx(hinst, dwVersion, riidltf, ppvOut, punkOuter);
    if (!hr_failed(hr) && ppvOut && *ppvOut)
        *ppvOut = wrap_dinput(*ppvOut);
    return hr;
}

static HRESULT WINAPI Hook_DirectInputCreateA(
    HINSTANCE hinst, DWORD dwVersion, void **ppvOut, void *punkOuter)
{
    HRESULT hr;

    if (!g_realDirectInputCreateA)
        return DDERR_GENERIC;

    hr = g_realDirectInputCreateA(hinst, dwVersion, ppvOut, punkOuter);
    LOG("DirectInputCreateA hook version=%08X hr=%08X out=%p",
        dwVersion, (DWORD)hr, ppvOut ? *ppvOut : NULL);
    if (!hr_failed(hr) && ppvOut && *ppvOut)
        *ppvOut = wrap_dinput(*ppvOut);
    return hr;
}

static void hook_dinput_iat(void)
{
    HMODULE module;
    HMODULE dinput;
    FARPROC dinputCreateEx;
    FARPROC dinputCreateA;
    BYTE *base;
    IMAGE_DOS_HEADER *dos;
    IMAGE_NT_HEADERS32 *nt;
    IMAGE_IMPORT_DESCRIPTOR *imp;
    DWORD importRva;
    DWORD importSize;
    BOOL sawDInput = FALSE;
    DWORD hookCount = 0;

    if (g_dinputHooked)
        return;

    dinput = LoadLibraryA("DINPUT.dll");
    dinputCreateEx = dinput ? GetProcAddress(dinput, "DirectInputCreateEx") : NULL;
    dinputCreateA = dinput ? GetProcAddress(dinput, "DirectInputCreateA") : NULL;

    module = GetModuleHandleA(NULL);
    if (!module) {
        LOG("DirectInput hook: GetModuleHandle(NULL) failed");
        return;
    }

    base = (BYTE *)module;
    dos = (IMAGE_DOS_HEADER *)base;
    if (dos->e_magic != IMAGE_DOS_SIGNATURE) {
        LOG("DirectInput hook: bad DOS signature base=%p", (void *)base);
        return;
    }

    nt = (IMAGE_NT_HEADERS32 *)(base + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE) {
        LOG("DirectInput hook: bad NT signature base=%p e_lfanew=%ld",
            (void *)base, dos->e_lfanew);
        return;
    }

    if (nt->OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR32_MAGIC) {
        LOG("DirectInput hook: unexpected optional header magic=%04X",
            nt->OptionalHeader.Magic);
        return;
    }

    importRva = nt->OptionalHeader
        .DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT].VirtualAddress;
    importSize = nt->OptionalHeader
        .DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT].Size;

    LOG("DirectInput hook: module=%p importRva=%08X importSize=%08X dinput=%p procEx=%p procA=%p",
        (void *)module, importRva, importSize, (void *)dinput,
        (void *)dinputCreateEx, (void *)dinputCreateA);

    if (!importRva) {
        LOG("DirectInput hook: no import directory");
        return;
    }

    for (imp = (IMAGE_IMPORT_DESCRIPTOR *)(base + importRva); imp->Name; ++imp) {
        const char *dllName = (const char *)(base + imp->Name);
        IMAGE_THUNK_DATA *nameThunk;
        IMAGE_THUNK_DATA *iatThunk;
        BOOL isDInputDll;

        isDInputDll =
            lstrcmpiA(dllName, "dinput.dll") == 0 ||
            lstrcmpiA(dllName, "dinput8.dll") == 0;
        LOG("DirectInput hook: import dll=%s oft=%08X ft=%08X%s",
            dllName, imp->OriginalFirstThunk, imp->FirstThunk,
            isDInputDll ? " target" : "");

        if (!isDInputDll)
            continue;
        sawDInput = TRUE;

        nameThunk = (IMAGE_THUNK_DATA *)(base +
            (imp->OriginalFirstThunk ? imp->OriginalFirstThunk : imp->FirstThunk));
        iatThunk = (IMAGE_THUNK_DATA *)(base + imp->FirstThunk);

        for (; nameThunk->u1.AddressOfData; ++nameThunk, ++iatThunk) {
            IMAGE_IMPORT_BY_NAME *importName;
            DWORD oldProtect;
            const char *funcName = NULL;

            if (!IMAGE_SNAP_BY_ORDINAL(nameThunk->u1.Ordinal)) {
                importName = (IMAGE_IMPORT_BY_NAME *)(base + nameThunk->u1.AddressOfData);
                funcName = (const char *)importName->Name;
                LOG("DirectInput hook: candidate name=%s iat=%p value=%p",
                    funcName, (void *)&iatThunk->u1.Function,
                    (void *)(ULONG_PTR)iatThunk->u1.Function);
            } else {
                LOG("DirectInput hook: candidate ordinal=%lu iat=%p value=%p",
                    (DWORD)(nameThunk->u1.Ordinal & 0xFFFF),
                    (void *)&iatThunk->u1.Function,
                    (void *)(ULONG_PTR)iatThunk->u1.Function);
            }

            if (((funcName && lstrcmpA(funcName, "DirectInputCreateEx") == 0) ||
                 (dinputCreateEx &&
                  (FARPROC)(ULONG_PTR)iatThunk->u1.Function == dinputCreateEx)) &&
                (FARPROC)(ULONG_PTR)iatThunk->u1.Function !=
                    (FARPROC)Hook_DirectInputCreateEx) {
                g_realDirectInputCreateEx =
                    (PFN_DirectInputCreateEx)(ULONG_PTR)iatThunk->u1.Function;
                if (!VirtualProtect(&iatThunk->u1.Function,
                                    sizeof(iatThunk->u1.Function),
                                    PAGE_READWRITE, &oldProtect)) {
                    LOG("DirectInput hook: VirtualProtect failed iat=%p lastError=%lu",
                        (void *)&iatThunk->u1.Function, GetLastError());
                    return;
                }
                iatThunk->u1.Function = (ULONG_PTR)Hook_DirectInputCreateEx;
                VirtualProtect(&iatThunk->u1.Function,
                               sizeof(iatThunk->u1.Function), oldProtect,
                               &oldProtect);
                FlushInstructionCache(GetCurrentProcess(),
                                      &iatThunk->u1.Function,
                                      sizeof(iatThunk->u1.Function));
                hookCount++;
                LOG("DirectInputCreateEx IAT hook installed from %s", dllName);
                continue;
            }

            if (((funcName && lstrcmpA(funcName, "DirectInputCreateA") == 0) ||
                 (dinputCreateA &&
                  (FARPROC)(ULONG_PTR)iatThunk->u1.Function == dinputCreateA)) &&
                (FARPROC)(ULONG_PTR)iatThunk->u1.Function !=
                    (FARPROC)Hook_DirectInputCreateA) {
                g_realDirectInputCreateA =
                    (PFN_DirectInputCreateA)(ULONG_PTR)iatThunk->u1.Function;
                if (!VirtualProtect(&iatThunk->u1.Function,
                                    sizeof(iatThunk->u1.Function),
                                    PAGE_READWRITE, &oldProtect)) {
                    LOG("DirectInput hook: VirtualProtect failed iat=%p lastError=%lu",
                        (void *)&iatThunk->u1.Function, GetLastError());
                    return;
                }
                iatThunk->u1.Function = (ULONG_PTR)Hook_DirectInputCreateA;
                VirtualProtect(&iatThunk->u1.Function,
                               sizeof(iatThunk->u1.Function), oldProtect,
                               &oldProtect);
                FlushInstructionCache(GetCurrentProcess(),
                                      &iatThunk->u1.Function,
                                      sizeof(iatThunk->u1.Function));
                hookCount++;
                LOG("DirectInputCreateA IAT hook installed from %s", dllName);
            }
        }
    }

    if (hookCount > 0) {
        g_dinputHooked = TRUE;
        return;
    }

    LOG("DirectInput hook: not installed sawDInput=%d realProcEx=%p realProcA=%p",
        sawDInput, (void *)dinputCreateEx, (void *)dinputCreateA);
}

/* ═══════════════════════════════════════════════════════════════════════════
   Logging
   ═══════════════════════════════════════════════════════════════════════════ */
static void log_init(void)
{
#if !DDRAW_WRAPPER_ENABLE_LOGGING
    return;
#else
    if (!g_logCSInit) {
        InitializeCriticalSection(&g_logCS);
        g_logCSInit = TRUE;
    }
    g_log = fopen("ddraw_wrapper.log", "w");
    if (!g_log) return;

    SYSTEMTIME st;
    GetLocalTime(&st);
    fprintf(g_log,
        "============================================================\n"
        "  EFZ DirectDraw Wrapper  –  built " __DATE__ "\n"
        "  F11  =  toggle borderless fullscreen\n"
        "  Supports: EFZ 1.11 / BSE 2.13 / BME 3.03 / Memorial 4.00\n"
        "============================================================\n"
        "  Started %04d-%02d-%02d %02d:%02d:%02d\n\n",
        st.wYear, st.wMonth, st.wDay,
        st.wHour, st.wMinute, st.wSecond);
    fflush(g_log);
#endif
}

#if !DDRAW_WRAPPER_ENABLE_LOGGING
/* LOG is compiled out in release builds. */
#else
static void LOG(const char *fmt, ...)
{
    if (!g_log) return;
    EnterCriticalSection(&g_logCS);
    SYSTEMTIME st;
    GetLocalTime(&st);
    fprintf(g_log, "[%02d:%02d:%02d.%03d] ",
            st.wHour, st.wMinute, st.wSecond, st.wMilliseconds);
    va_list ap;
    va_start(ap, fmt);
    vfprintf(g_log, fmt, ap);
    va_end(ap);
    fputc('\n', g_log);
    fflush(g_log);
    LeaveCriticalSection(&g_logCS);
}
#endif

/* ═══════════════════════════════════════════════════════════════════════════
   Surface pixel buffer helpers
   ═══════════════════════════════════════════════════════════════════════════ */
static BYTE *alloc_pixels(int w, int h, int *pitchOut)
{
    int pitch = (w + 3) & ~3;          /* 4-byte row alignment */
    *pitchOut = pitch;
    return (BYTE *)calloc((size_t)(pitch * h), 1);
}

/* ═══════════════════════════════════════════════════════════════════════════
   GDI frame presentation  (called from IDirectDrawSurface::Flip)
   ═══════════════════════════════════════════════════════════════════════════ */

/* Compute a centred letterbox/pillarbox rect that preserves the game's
   aspect ratio within (screenW x screenH). */
static void compute_letterbox(int screenW, int screenH, int gameW, int gameH,
                               int *outX, int *outY, int *outW, int *outH)
{
    int w = screenW;
    int h = (gameW > 0) ? screenW * gameH / gameW : screenH;
    if (h > screenH) {
        h = screenH;
        w = (gameH > 0) ? screenH * gameW / gameH : screenW;
    }
    *outX = (screenW - w) / 2;
    *outY = (screenH - h) / 2;
    *outW = w;
    *outH = h;
}

static DWORD sample_surface_hash(MyDDSurface *src)
{
    DWORD hash = 2166136261u;
    int total;
    int step;

    if (!src || !src->pixels || src->width <= 0 || src->height <= 0)
        return 0;

    total = src->pitch * src->height;
    step = total / 1024;
    if (step <= 0) step = 1;

    for (int i = 0; i < total; i += step) {
        hash ^= src->pixels[i];
        hash *= 16777619u;
    }
    return hash;
}

static void present_frame(MyDDraw *dd)
{
    DWORD presentNo = ++g_presentSerial;
    BOOL traceThisPresent = FALSE;

    if (g_renderTraceAfterFocus && g_renderTraceBudget > 0) {
        traceThisPresent = TRUE;
        g_renderTraceBudget--;
        if (g_renderTraceBudget == 0)
            g_renderTraceAfterFocus = FALSE;
    } else if (presentNo <= 8 || (presentNo % 300) == 0) {
        traceThisPresent = TRUE;
    }

    if (!dd || !dd->hWnd) {
        LOG("Render present skipped frame=%u reason=no-dd-or-hwnd dd=%p",
            presentNo, (void *)dd);
        return;
    }

    /* The rendered content lives in the back-buffer */
    MyDDSurface *src = (dd->primarySurface && dd->primarySurface->attachedBackBuffer)
                     ? dd->primarySurface->attachedBackBuffer
                     : dd->primarySurface;
    if (!src || !src->pixels) {
        LOG("Render present skipped frame=%u reason=no-source primary=%p back=%p active=%d focusSerial=%u",
            presentNo,
            dd->primarySurface ? (void *)dd->primarySurface : NULL,
            (dd->primarySurface && dd->primarySurface->attachedBackBuffer)
                ? (void *)dd->primarySurface->attachedBackBuffer : NULL,
            g_appActive, g_focusSerial);
        return;
    }

    /* Build a DIB header that describes the 8-bit indexed surface */
    struct {
        BITMAPINFOHEADER bmiHeader;
        RGBQUAD          bmiColors[256];
    } bmi;
    ZeroMemory(&bmi, sizeof(bmi));
    bmi.bmiHeader.biSize        = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth       = src->width;
    bmi.bmiHeader.biHeight      = -(src->height);  /* negative = top-down */
    bmi.bmiHeader.biPlanes      = 1;
    bmi.bmiHeader.biBitCount    = 8;
    bmi.bmiHeader.biCompression = BI_RGB;
    bmi.bmiHeader.biClrUsed     = 256;

    /* Resolve palette: back-buffer palette → primary palette → blank */
    MyDDPalette *pal = src->palette
                     ? src->palette
                     : (dd->primarySurface ? dd->primarySurface->palette : NULL);
    if (pal)
        memcpy(bmi.bmiColors, pal->entries, 256 * sizeof(RGBQUAD));

    RECT cr;
    GetClientRect(dd->hWnd, &cr);
    int fullW = cr.right  - cr.left;
    int fullH = cr.bottom - cr.top;
    if (fullW <= 0 || fullH <= 0) {
        LOG("Render present skipped frame=%u reason=empty-client client=%dx%d hwnd=%p active=%d focusSerial=%u",
            presentNo, fullW, fullH, (void *)dd->hWnd, g_appActive,
            g_focusSerial);
        return;
    }

    HDC hDC = GetDC(dd->hWnd);
    if (!hDC) {
        LOG("Render present skipped frame=%u reason=GetDC-failed hwnd=%p active=%d focusSerial=%u lastError=%lu",
            presentNo, (void *)dd->hWnd, g_appActive, g_focusSerial,
            GetLastError());
        return;
    }

    int dstX, dstY, dstW, dstH;
    if (dd->borderless && src->width > 0 && src->height > 0) {
        compute_letterbox(fullW, fullH, src->width, src->height,
                          &dstX, &dstY, &dstW, &dstH);
        /* Paint black bars */
        RECT full = { 0, 0, fullW, fullH };
        FillRect(hDC, &full, (HBRUSH)GetStockObject(BLACK_BRUSH));
    } else {
        dstX = 0; dstY = 0; dstW = fullW; dstH = fullH;
    }

    SetStretchBltMode(hDC, COLORONCOLOR);
    int stretchResult = StretchDIBits(hDC,
                                      dstX, dstY, dstW, dstH,
                                      0, 0, src->width, src->height,
                                      src->pixels,
                                      (BITMAPINFO *)&bmi,
                                      DIB_RGB_COLORS,
                                      SRCCOPY);
    if (traceThisPresent || stretchResult == GDI_ERROR || stretchResult == 0) {
        LOG("Render present frame=%u flip=%u active=%d focusSerial=%u focusAgeMs=%lu hwnd=%p client=%dx%d src=%dx%d pitch=%d dst=%d,%d %dx%d borderless=%d pal=%d hash=%08X StretchDIBits=%d lastError=%lu",
            presentNo, g_flipSerial, g_appActive, g_focusSerial,
            GetTickCount() - g_lastFocusTick, (void *)dd->hWnd,
            fullW, fullH, src->width, src->height, src->pitch,
            dstX, dstY, dstW, dstH, dd->borderless, pal != NULL,
            sample_surface_hash(src), stretchResult, GetLastError());
    }
    ReleaseDC(dd->hWnd, hDC);
}

/* ═══════════════════════════════════════════════════════════════════════════
   Window mode management
   ═══════════════════════════════════════════════════════════════════════════ */
static void set_windowed_mode(MyDDraw *dd)
{
    HWND hWnd = dd->hWnd;
    if (!hWnd || dd->gameWidth <= 0) return;

    LONG style   = (WS_OVERLAPPEDWINDOW) & ~(WS_THICKFRAME | WS_MAXIMIZEBOX);
    LONG exStyle = WS_EX_APPWINDOW;

    SetWindowLongA(hWnd, GWL_STYLE,   style);
    SetWindowLongA(hWnd, GWL_EXSTYLE, exStyle);

    /* Grow the outer rect to fit the game's client area */
    RECT wr = { 0, 0, dd->gameWidth, dd->gameHeight };
    AdjustWindowRectEx(&wr, style, FALSE, exStyle);
    int winW = wr.right  - wr.left;
    int winH = wr.bottom - wr.top;

    /* Centre on primary monitor */
    int screenW = GetSystemMetrics(SM_CXSCREEN);
    int screenH = GetSystemMetrics(SM_CYSCREEN);
    int posX    = (screenW - winW) / 2;
    int posY    = (screenH - winH) / 2;
    if (posX < 0) posX = 0;
    if (posY < 0) posY = 0;

    SetWindowPos(hWnd, HWND_TOP,
                 posX, posY, winW, winH,
                 SWP_NOZORDER | SWP_FRAMECHANGED | SWP_SHOWWINDOW);

    dd->borderless = FALSE;
    LOG("Window mode: windowed %dx%d (game %dx%d) at (%d,%d)",
        winW, winH, dd->gameWidth, dd->gameHeight, posX, posY);
}

static void set_borderless_mode(MyDDraw *dd)
{
    HWND hWnd = dd->hWnd;
    if (!hWnd) return;

    if (!dd->borderless) {
        GetWindowRect(hWnd, &dd->savedWinRect);
        dd->savedStyle   = GetWindowLongA(hWnd, GWL_STYLE);
        dd->savedExStyle = GetWindowLongA(hWnd, GWL_EXSTYLE);
    }

    SetWindowLongA(hWnd, GWL_STYLE,   WS_POPUP | WS_VISIBLE);
    SetWindowLongA(hWnd, GWL_EXSTYLE, WS_EX_APPWINDOW);

    int screenW = GetSystemMetrics(SM_CXSCREEN);
    int screenH = GetSystemMetrics(SM_CYSCREEN);
    SetWindowPos(hWnd, HWND_TOP,
                 0, 0, screenW, screenH,
                 SWP_NOZORDER | SWP_FRAMECHANGED | SWP_SHOWWINDOW);

    dd->borderless = TRUE;
    LOG("Window mode: borderless %dx%d", screenW, screenH);
}

static void toggle_window_mode(MyDDraw *dd)
{
    if (dd->borderless)
        set_windowed_mode(dd);
    else
        set_borderless_mode(dd);
}

/* ═══════════════════════════════════════════════════════════════════════════
   Low-level keyboard hook  –  catches F11 even when the game swallows keys
   ═══════════════════════════════════════════════════════════════════════════ */
static LRESULT CALLBACK LLKeyboardProc(int nCode, WPARAM wParam, LPARAM lParam)
{
    if (nCode == HC_ACTION && (wParam == WM_KEYDOWN || wParam == WM_SYSKEYDOWN)) {
        KBDLLHOOKSTRUCT *kb = (KBDLLHOOKSTRUCT *)lParam;
        if (kb->vkCode == VK_F11 && g_dd && g_appActive &&
            GetForegroundWindow() == g_dd->hWnd) {
            toggle_window_mode(g_dd);
            return 1;   /* swallow this key event */
        }
    }
    return CallNextHookEx(g_kbHook, nCode, wParam, lParam);
}

/* ═══════════════════════════════════════════════════════════════════════════
   WndProc hook  –  F11 toggle, WM_PAINT re-present, activation tracking
   ═══════════════════════════════════════════════════════════════════════════ */
static LRESULT CALLBACK WrapperWndProc(HWND hWnd, UINT msg,
                                        WPARAM wParam, LPARAM lParam)
{
    if (msg == WM_ACTIVATE) {
        BOOL newActive = (LOWORD(wParam) != WA_INACTIVE);
        if (newActive != g_appActive || LOWORD(wParam) == WA_INACTIVE) {
            g_focusSerial++;
            g_lastFocusTick = GetTickCount();
            g_renderTraceAfterFocus = TRUE;
            g_renderTraceBudget = 20;
            g_wndTraceBudget = 80;
            LOG("Focus WM_ACTIVATE serial=%u active %d->%d state=%u minimized=%u hwnd=%p other=%p fg=%p",
                g_focusSerial, g_appActive, newActive, LOWORD(wParam),
                HIWORD(wParam), (void *)hWnd, (void *)lParam,
                (void *)GetForegroundWindow());
        }
        g_appActive = newActive;
        /* Forward to game so it can Unacquire/Acquire DirectInput on focus change */
        if (g_dd && g_dd->origWndProc)
            return CallWindowProcA(g_dd->origWndProc, hWnd, msg, wParam, lParam);
        return DefWindowProcA(hWnd, msg, wParam, lParam);
    } else if (msg == WM_ACTIVATEAPP) {
        BOOL newActive = (wParam != FALSE);
        if (newActive != g_appActive || !newActive) {
            g_focusSerial++;
            g_lastFocusTick = GetTickCount();
            g_renderTraceAfterFocus = TRUE;
            g_renderTraceBudget = 20;
            g_wndTraceBudget = 80;
            LOG("Focus WM_ACTIVATEAPP serial=%u active %d->%d thread=%lu hwnd=%p fg=%p",
                g_focusSerial, g_appActive, newActive, (DWORD)lParam,
                (void *)hWnd, (void *)GetForegroundWindow());
        }
        g_appActive = newActive;
        /* Forward to game so it can Unacquire/Acquire DirectInput on focus change */
        if (g_dd && g_dd->origWndProc)
            return CallWindowProcA(g_dd->origWndProc, hWnd, msg, wParam, lParam);
        return DefWindowProcA(hWnd, msg, wParam, lParam);
    } else if (msg == WM_SETFOCUS || msg == WM_KILLFOCUS) {
        LOG("Focus %s active=%d hwnd=%p other=%p fg=%p focusSerial=%u",
            msg == WM_SETFOCUS ? "WM_SETFOCUS" : "WM_KILLFOCUS",
            g_appActive, (void *)hWnd, (void *)wParam,
            (void *)GetForegroundWindow(), g_focusSerial);
        if (g_dd && g_dd->origWndProc)
            return CallWindowProcA(g_dd->origWndProc, hWnd, msg, wParam, lParam);
        return DefWindowProcA(hWnd, msg, wParam, lParam);
    } else if (msg == WM_PAINT && g_renderTraceAfterFocus) {
        LOG("Render WM_PAINT after-focus active=%d hwnd=%p primary=%p focusSerial=%u",
            g_appActive, (void *)hWnd,
            g_dd ? (void *)g_dd->primarySurface : NULL, g_focusSerial);
    } else if ((msg == WM_SIZE || msg == WM_WINDOWPOSCHANGED) && g_renderTraceAfterFocus) {
        LOG("Window msg=%s active=%d hwnd=%p wParam=%08X lParam=%08X focusSerial=%u",
            msg == WM_SIZE ? "WM_SIZE" : "WM_WINDOWPOSCHANGED",
            g_appActive, (void *)hWnd, (DWORD)wParam, (DWORD)lParam,
            g_focusSerial);
    } else if (g_wndTraceBudget > 0) {
        LOG("Window msg after-focus msg=%04X active=%d hwnd=%p wParam=%08X lParam=%08X focusSerial=%u",
            msg, g_appActive, (void *)hWnd, (DWORD)wParam, (DWORD)lParam,
            g_focusSerial);
        g_wndTraceBudget--;
    }

    /* Intercept WM_SYSCOMMAND before the game sees it.
     * Old fullscreen games swallow SC_KEYMENU (Alt key), SC_SCREENSAVE and
     * SC_MONITORPOWER.  We must let SC_KEYMENU reach DefWindowProc so that
     * Alt-based system shortcuts (Alt+F4, system menu) keep working, and we
     * suppress the screen-saver / monitor-off commands ourselves so the game
     * does not have to handle them. */
    if (msg == WM_SYSCOMMAND) {
        WPARAM cmd = wParam & 0xFFF0;
        if (cmd == SC_SCREENSAVE || cmd == SC_MONITORPOWER)
            return 0;
        if (cmd == SC_KEYMENU)
            return DefWindowProcA(hWnd, msg, wParam, lParam);
    }

    /* Prevent the default black-erase before we paint our frame */
    if (msg == WM_ERASEBKGND) return 1;

    if (msg == WM_PAINT && g_dd && g_dd->primarySurface) {
        PAINTSTRUCT ps;
        BeginPaint(hWnd, &ps);
        present_frame(g_dd);
        EndPaint(hWnd, &ps);
        return 0;
    }
    if (g_dd && g_dd->origWndProc)
        return CallWindowProcA(g_dd->origWndProc, hWnd, msg, wParam, lParam);
    return DefWindowProcA(hWnd, msg, wParam, lParam);
}

/* ═══════════════════════════════════════════════════════════════════════════
   IDirectDrawPalette  implementation
   ═══════════════════════════════════════════════════════════════════════════ */
static ULONG   __stdcall DDPal_AddRef(MyDDPalette *p)
    { return (ULONG)InterlockedIncrement(&p->refCount); }

static ULONG   __stdcall DDPal_Release(MyDDPalette *p)
{
    LONG ref = InterlockedDecrement(&p->refCount);
    if (ref == 0) { LOG("IDirectDrawPalette freed"); free(p); }
    return (ULONG)ref;
}

static HRESULT __stdcall DDPal_QueryInterface(MyDDPalette *p, REFIID r, void **ppv)
    { *ppv = p; DDPal_AddRef(p); return DD_OK; }

static HRESULT __stdcall DDPal_GetCaps(MyDDPalette *p, DWORD *pdwCaps)
    { if (pdwCaps) *pdwCaps = DDPCAPS_8BIT | DDPCAPS_ALLOW256; return DD_OK; }

static HRESULT __stdcall DDPal_GetEntries(MyDDPalette *p,
    DWORD dwFlags, DWORD dwBase, DWORD dwNumEntries, PALETTEENTRY *pEntries)
{
    for (DWORD i = 0; i < dwNumEntries && (dwBase + i) < 256; i++) {
        pEntries[i].peRed   = p->entries[dwBase + i].rgbRed;
        pEntries[i].peGreen = p->entries[dwBase + i].rgbGreen;
        pEntries[i].peBlue  = p->entries[dwBase + i].rgbBlue;
        pEntries[i].peFlags = 0;
    }
    return DD_OK;
}

static HRESULT __stdcall DDPal_Initialize(MyDDPalette *p, void *pDD,
    DWORD dwFlags, PALETTEENTRY *pEntries) { return DDERR_UNSUPPORTED; }

static HRESULT __stdcall DDPal_SetEntries(MyDDPalette *p,
    DWORD dwFlags, DWORD dwBase, DWORD dwNumEntries, PALETTEENTRY *pEntries)
{
    for (DWORD i = 0; i < dwNumEntries && (dwBase + i) < 256; i++) {
        p->entries[dwBase + i].rgbRed      = pEntries[i].peRed;
        p->entries[dwBase + i].rgbGreen    = pEntries[i].peGreen;
        p->entries[dwBase + i].rgbBlue     = pEntries[i].peBlue;
        p->entries[dwBase + i].rgbReserved = 0;
    }
    return DD_OK;
}

/* IDirectDrawPalette vtable  (7 entries, SDK order) */
static void *g_DDPalVtbl[] = {
    (void *)DDPal_QueryInterface,   /* 0 */
    (void *)DDPal_AddRef,           /* 1 */
    (void *)DDPal_Release,          /* 2 */
    (void *)DDPal_GetCaps,          /* 3 */
    (void *)DDPal_GetEntries,       /* 4 */
    (void *)DDPal_Initialize,       /* 5 */
    (void *)DDPal_SetEntries,       /* 6 */
};

/* ═══════════════════════════════════════════════════════════════════════════
   IDirectDrawSurface  implementation
   ═══════════════════════════════════════════════════════════════════════════ */

/* ── Forward declarations (needed so the vtable array can be defined before
      the function bodies, which is required by MSVC C)                    ── */
static HRESULT __stdcall DDSurf_QueryInterface(MyDDSurface *, REFIID, void **);
static ULONG   __stdcall DDSurf_AddRef(MyDDSurface *);
static ULONG   __stdcall DDSurf_Release(MyDDSurface *);
static HRESULT __stdcall DDSurf_AddAttachedSurface(MyDDSurface *, MyDDSurface *);
static HRESULT __stdcall DDSurf_AddOverlayDirtyRect(MyDDSurface *, RECT *);
static HRESULT __stdcall DDSurf_Blt(MyDDSurface *, RECT *, MyDDSurface *, RECT *, DWORD, void *);
static HRESULT __stdcall DDSurf_BltBatch(MyDDSurface *, void *, DWORD, DWORD);
static HRESULT __stdcall DDSurf_BltFast(MyDDSurface *, DWORD, DWORD, MyDDSurface *, RECT *, DWORD);
static HRESULT __stdcall DDSurf_DeleteAttachedSurface(MyDDSurface *, DWORD, MyDDSurface *);
static HRESULT __stdcall DDSurf_EnumAttachedSurfaces(MyDDSurface *, void *, void *);
static HRESULT __stdcall DDSurf_EnumOverlayZOrders(MyDDSurface *, DWORD, void *, void *);
static HRESULT __stdcall DDSurf_Flip(MyDDSurface *, MyDDSurface *, DWORD);
static HRESULT __stdcall DDSurf_GetAttachedSurface(MyDDSurface *, MY_DDSCAPS *, MyDDSurface **);
static HRESULT __stdcall DDSurf_GetBltStatus(MyDDSurface *, DWORD);
static HRESULT __stdcall DDSurf_GetCaps(MyDDSurface *, MY_DDSCAPS *);
static HRESULT __stdcall DDSurf_GetClipper(MyDDSurface *, void **);
static HRESULT __stdcall DDSurf_GetColorKey(MyDDSurface *, DWORD, MY_DDCOLORKEY *);
static HRESULT __stdcall DDSurf_GetDC(MyDDSurface *, HDC *);
static HRESULT __stdcall DDSurf_GetFlipStatus(MyDDSurface *, DWORD);
static HRESULT __stdcall DDSurf_GetOverlayPosition(MyDDSurface *, LONG *, LONG *);
static HRESULT __stdcall DDSurf_GetPalette(MyDDSurface *, MyDDPalette **);
static HRESULT __stdcall DDSurf_GetPixelFormat(MyDDSurface *, MY_DDPIXELFORMAT *);
static HRESULT __stdcall DDSurf_GetSurfaceDesc(MyDDSurface *, MY_DDSURFACEDESC *);
static HRESULT __stdcall DDSurf_Initialize(MyDDSurface *, void *, MY_DDSURFACEDESC *);
static HRESULT __stdcall DDSurf_IsLost(MyDDSurface *);
static HRESULT __stdcall DDSurf_Lock(MyDDSurface *, RECT *, MY_DDSURFACEDESC *, DWORD, HANDLE);
static HRESULT __stdcall DDSurf_ReleaseDC(MyDDSurface *, HDC);
static HRESULT __stdcall DDSurf_Restore(MyDDSurface *);
static HRESULT __stdcall DDSurf_SetClipper(MyDDSurface *, void *);
static HRESULT __stdcall DDSurf_SetColorKey(MyDDSurface *, DWORD, MY_DDCOLORKEY *);
static HRESULT __stdcall DDSurf_SetOverlayPosition(MyDDSurface *, LONG, LONG);
static HRESULT __stdcall DDSurf_SetPalette(MyDDSurface *, MyDDPalette *);
static HRESULT __stdcall DDSurf_Unlock(MyDDSurface *, void *);
static HRESULT __stdcall DDSurf_UpdateOverlay(MyDDSurface *, RECT *, MyDDSurface *, RECT *, DWORD, void *);
static HRESULT __stdcall DDSurf_UpdateOverlayDisplay(MyDDSurface *, DWORD);
static HRESULT __stdcall DDSurf_UpdateOverlayZOrder(MyDDSurface *, DWORD, MyDDSurface *);

/*
 * IDirectDrawSurface vtable  –  36 entries, exact SDK order.
 *
 * Offsets the EFZ games are known to call:
 *   +  8  Release
 *   + 20  Blt
 *   + 28  BltFast
 *   + 44  Flip              (primary surface)
 *   + 48  GetAttachedSurface
 *   +100  Lock
 *   +116  SetColorKey
 *   +124  SetPalette
 *   +128  Unlock
 */
static void *g_DDSurfVtbl[] = {
    (void *)DDSurf_QueryInterface,          /*  0  +  0 */
    (void *)DDSurf_AddRef,                  /*  1  +  4 */
    (void *)DDSurf_Release,                 /*  2  +  8 */
    (void *)DDSurf_AddAttachedSurface,      /*  3  + 12 */
    (void *)DDSurf_AddOverlayDirtyRect,     /*  4  + 16 */
    (void *)DDSurf_Blt,                     /*  5  + 20 */
    (void *)DDSurf_BltBatch,                /*  6  + 24 */
    (void *)DDSurf_BltFast,                 /*  7  + 28 */
    (void *)DDSurf_DeleteAttachedSurface,   /*  8  + 32 */
    (void *)DDSurf_EnumAttachedSurfaces,    /*  9  + 36 */
    (void *)DDSurf_EnumOverlayZOrders,      /* 10  + 40 */
    (void *)DDSurf_Flip,                    /* 11  + 44 */
    (void *)DDSurf_GetAttachedSurface,      /* 12  + 48 */
    (void *)DDSurf_GetBltStatus,            /* 13  + 52 */
    (void *)DDSurf_GetCaps,                 /* 14  + 56 */
    (void *)DDSurf_GetClipper,              /* 15  + 60 */
    (void *)DDSurf_GetColorKey,             /* 16  + 64 */
    (void *)DDSurf_GetDC,                   /* 17  + 68 */
    (void *)DDSurf_GetFlipStatus,           /* 18  + 72 */
    (void *)DDSurf_GetOverlayPosition,      /* 19  + 76 */
    (void *)DDSurf_GetPalette,              /* 20  + 80 */
    (void *)DDSurf_GetPixelFormat,          /* 21  + 84 */
    (void *)DDSurf_GetSurfaceDesc,          /* 22  + 88 */
    (void *)DDSurf_Initialize,              /* 23  + 92 */
    (void *)DDSurf_IsLost,                  /* 24  + 96 */
    (void *)DDSurf_Lock,                    /* 25  +100 */
    (void *)DDSurf_ReleaseDC,               /* 26  +104 */
    (void *)DDSurf_Restore,                 /* 27  +108 */
    (void *)DDSurf_SetClipper,              /* 28  +112 */
    (void *)DDSurf_SetColorKey,             /* 29  +116 */
    (void *)DDSurf_SetOverlayPosition,      /* 30  +120 */
    (void *)DDSurf_SetPalette,              /* 31  +124 */
    (void *)DDSurf_Unlock,                  /* 32  +128 */
    (void *)DDSurf_UpdateOverlay,           /* 33  +132 */
    (void *)DDSurf_UpdateOverlayDisplay,    /* 34  +136 */
    (void *)DDSurf_UpdateOverlayZOrder,     /* 35  +140 */
};

static MyDDSurface *surf_create(MyDDraw *dd, int w, int h,
                                 BOOL isPrimary, BOOL isBackBuffer)
{
    MyDDSurface *s = (MyDDSurface *)calloc(1, sizeof(MyDDSurface));
    if (!s) return NULL;
    s->lpVtbl      = g_DDSurfVtbl;
    s->refCount    = 1;
    s->width       = w;
    s->height      = h;
    s->pixels      = alloc_pixels(w, h, &s->pitch);
    s->isPrimary   = isPrimary;
    s->isBackBuffer = isBackBuffer;
    s->owner       = dd;
    return s;
}

static ULONG __stdcall DDSurf_AddRef(MyDDSurface *s)
    { return (ULONG)InterlockedIncrement(&s->refCount); }

static ULONG __stdcall DDSurf_Release(MyDDSurface *s)
{
    LONG ref = InterlockedDecrement(&s->refCount);
    if (ref == 0) {
        LOG("IDirectDrawSurface freed (%s %dx%d)",
            s->isPrimary ? "primary" : s->isBackBuffer ? "backbuf" : "offscreen",
            s->width, s->height);
        /* Release attached back-buffer (holds one implicit ref) */
        if (s->attachedBackBuffer) {
            DDSurf_Release(s->attachedBackBuffer);
            s->attachedBackBuffer = NULL;
        }
        if (s->palette) {
            DDPal_Release(s->palette);
            s->palette = NULL;
        }
        /* Clear primary pointer in the owning DD object */
        if (s->isPrimary && s->owner && s->owner->primarySurface == s)
            s->owner->primarySurface = NULL;
        free(s->pixels);
        free(s);
    }
    return (ULONG)ref;
}

static HRESULT __stdcall DDSurf_QueryInterface(MyDDSurface *s, REFIID r, void **ppv)
    { *ppv = s; DDSurf_AddRef(s); return DD_OK; }

static HRESULT __stdcall DDSurf_AddAttachedSurface(MyDDSurface *s, MyDDSurface *surf)
    { return DD_OK; }
static HRESULT __stdcall DDSurf_AddOverlayDirtyRect(MyDDSurface *s, RECT *r)
    { return DD_OK; }

/* ─── Blt ─────────────────────────────────────────────────────────────────── */
static HRESULT __stdcall DDSurf_Blt(MyDDSurface *dst, RECT *destRect,
                                     MyDDSurface *src, RECT *srcRect,
                                     DWORD flags, void *pFX)
{
    /* Color fill (pFX is DDBLTFX; dwFillColor sits at byte offset 76) */
    if (flags & DDBLT_COLORFILL) {
        BYTE fill = pFX ? (BYTE)(*(DWORD *)((BYTE *)pFX + 76)) : 0;
        RECT dr = { 0, 0, dst->width, dst->height };
        if (destRect) dr = *destRect;
        for (int y = dr.top; y < dr.bottom && y < dst->height; y++) {
            int left = dr.left  < 0 ? 0 : dr.left;
            int right= dr.right > dst->width ? dst->width : dr.right;
            if (right > left)
                memset(dst->pixels + y * dst->pitch + left, fill, right - left);
        }
        return DD_OK;
    }
    if (!src) return DDERR_INVALIDPARAMS;

    RECT sr = { 0, 0, src->width, src->height };
    RECT dr = { 0, 0, dst->width, dst->height };
    if (srcRect) sr = *srcRect;
    if (destRect) dr = *destRect;

    int sw = sr.right - sr.left;
    int sh = sr.bottom - sr.top;
    int dw = dr.right  - dr.left;
    int dh = dr.bottom - dr.top;
    if (sw <= 0 || sh <= 0 || dw <= 0 || dh <= 0) return DD_OK;

    BOOL ck = (flags & DDBLT_KEYSRC) && src->hasColorKeySrc;
    DWORD ckLo = src->colorKeySrcLow, ckHi = src->colorKeySrcHigh;

    for (int y = 0; y < dh; y++) {
        int sy = sr.top + y * sh / dh;
        if (sy < 0 || sy >= src->height) continue;
        for (int x = 0; x < dw; x++) {
            int sx = sr.left + x * sw / dw;
            if (sx < 0 || sx >= src->width) continue;
            BYTE px = src->pixels[sy * src->pitch + sx];
            if (ck && (DWORD)px >= ckLo && (DWORD)px <= ckHi) continue;
            int dy = dr.top + y, dx = dr.left + x;
            if (dy >= 0 && dy < dst->height && dx >= 0 && dx < dst->width)
                dst->pixels[dy * dst->pitch + dx] = px;
        }
    }
    return DD_OK;
}

static HRESULT __stdcall DDSurf_BltBatch(MyDDSurface *s, void *pBatch,
                                          DWORD dwCount, DWORD dwFlags)
    { return DD_OK; }

/* ─── BltFast ─────────────────────────────────────────────────────────────── */
static HRESULT __stdcall DDSurf_BltFast(MyDDSurface *dst, DWORD destX, DWORD destY,
                                         MyDDSurface *src, RECT *srcRect, DWORD flags)
{
    if (!src) return DDERR_INVALIDPARAMS;

    RECT sr = { 0, 0, src->width, src->height };
    if (srcRect) sr = *srcRect;
    int sw = sr.right - sr.left;
    int sh = sr.bottom - sr.top;
    if (sw <= 0 || sh <= 0) return DD_OK;

    BOOL ck  = (flags & DDBLTFAST_SRCCOLORKEY) && src->hasColorKeySrc;
    DWORD lo = src->colorKeySrcLow, hi = src->colorKeySrcHigh;

    for (int y = 0; y < sh; y++) {
        int dy = (int)destY + y;
        if (dy < 0 || dy >= dst->height) continue;
        const BYTE *sRow = src->pixels + (sr.top + y) * src->pitch + sr.left;
        BYTE       *dRow = dst->pixels + dy * dst->pitch + destX;

        if (!ck) {
            /* fast path: solid row copy */
            int copyW = sw;
            if ((int)destX + copyW > dst->width)
                copyW = dst->width - (int)destX;
            if (copyW > 0)
                memcpy(dRow, sRow, (size_t)copyW);
        } else {
            for (int x = 0; x < sw; x++) {
                if ((int)destX + x >= dst->width) break;
                BYTE px = sRow[x];
                if ((DWORD)px >= lo && (DWORD)px <= hi) continue;
                dRow[x] = px;
            }
        }
    }
    return DD_OK;
}

static HRESULT __stdcall DDSurf_DeleteAttachedSurface(MyDDSurface *s,
    DWORD dwFlags, MyDDSurface *surf) { return DD_OK; }
static HRESULT __stdcall DDSurf_EnumAttachedSurfaces(MyDDSurface *s,
    void *pCtx, void *pfn) { return DD_OK; }
static HRESULT __stdcall DDSurf_EnumOverlayZOrders(MyDDSurface *s,
    DWORD dwFlags, void *pCtx, void *pfn) { return DD_OK; }

/* ─── Flip ─────────────────────────────────────────────────────────────────── */
static HRESULT __stdcall DDSurf_Flip(MyDDSurface *s, MyDDSurface *target, DWORD flags)
{
    DWORD flipNo = ++g_flipSerial;

    if (flipNo <= 8 || g_renderTraceAfterFocus || (flipNo % 300) == 0) {
        LOG("DDSurf_Flip flip=%u surf=%p primary=%d owner=%p target=%p flags=%08X active=%d focusSerial=%u",
            flipNo, (void *)s, s ? s->isPrimary : 0,
            s ? (void *)s->owner : NULL, (void *)target, flags,
            g_appActive, g_focusSerial);
    }

    if (s && s->isPrimary && s->owner)
        present_frame(s->owner);
    else if (flipNo <= 8 || g_renderTraceAfterFocus) {
        LOG("DDSurf_Flip skipped-present flip=%u reason=%s",
            flipNo, !s ? "null-surface" :
            !s->isPrimary ? "not-primary" : "no-owner");
    }
    return DD_OK;
}

/* ─── GetAttachedSurface ──────────────────────────────────────────────────── */
static HRESULT __stdcall DDSurf_GetAttachedSurface(MyDDSurface *s,
    MY_DDSCAPS *pCaps, MyDDSurface **ppSurf)
{
    if (!ppSurf) return DDERR_INVALIDPARAMS;
    if (s->attachedBackBuffer) {
        DDSurf_AddRef(s->attachedBackBuffer);
        *ppSurf = s->attachedBackBuffer;
        LOG("GetAttachedSurface → back buffer %dx%d",
            s->attachedBackBuffer->width, s->attachedBackBuffer->height);
        return DD_OK;
    }
    *ppSurf = NULL;
    return DDERR_GENERIC;
}

static HRESULT __stdcall DDSurf_GetBltStatus(MyDDSurface *s, DWORD f)
    { return DD_OK; }

static HRESULT __stdcall DDSurf_GetCaps(MyDDSurface *s, MY_DDSCAPS *pCaps)
{
    if (pCaps)
        pCaps->dwCaps = s->isPrimary
                      ? (DDSCAPS_PRIMARYSURFACE | DDSCAPS_FLIP |
                         DDSCAPS_COMPLEX | DDSCAPS_SYSTEMMEMORY)
                      : s->isBackBuffer
                      ? (DDSCAPS_BACKBUFFER | DDSCAPS_FLIP | DDSCAPS_SYSTEMMEMORY)
                      : (DDSCAPS_OFFSCREENPLAIN | DDSCAPS_SYSTEMMEMORY);
    return DD_OK;
}

static HRESULT __stdcall DDSurf_GetClipper(MyDDSurface *s, void **pp)
    { return DDERR_UNSUPPORTED; }

static HRESULT __stdcall DDSurf_GetColorKey(MyDDSurface *s,
    DWORD dwFlags, MY_DDCOLORKEY *pCK)
{
    if (pCK && (dwFlags & DDCKEY_SRCBLT) && s->hasColorKeySrc) {
        pCK->dwLow  = s->colorKeySrcLow;
        pCK->dwHigh = s->colorKeySrcHigh;
    }
    return DD_OK;
}

static HRESULT __stdcall DDSurf_GetDC(MyDDSurface *s, HDC *phDC)
    { if (phDC) *phDC = NULL; return DDERR_UNSUPPORTED; }

static HRESULT __stdcall DDSurf_GetFlipStatus(MyDDSurface *s, DWORD f)
    { return DD_OK; }
static HRESULT __stdcall DDSurf_GetOverlayPosition(MyDDSurface *s, LONG *px, LONG *py)
    { return DDERR_UNSUPPORTED; }

static HRESULT __stdcall DDSurf_GetPalette(MyDDSurface *s, MyDDPalette **pp)
{
    if (!pp) return DDERR_INVALIDPARAMS;
    *pp = s->palette;
    if (s->palette) DDPal_AddRef(s->palette);
    return DD_OK;
}

static HRESULT __stdcall DDSurf_GetPixelFormat(MyDDSurface *s, MY_DDPIXELFORMAT *pFmt)
{
    if (!pFmt) return DDERR_INVALIDPARAMS;
    ZeroMemory(pFmt, sizeof(*pFmt));
    pFmt->dwSize        = sizeof(MY_DDPIXELFORMAT);
    pFmt->dwFlags       = 0x00000200;   /* DDPF_PALETTEINDEXED8 */
    pFmt->dwRGBBitCount = 8;
    return DD_OK;
}

static HRESULT __stdcall DDSurf_GetSurfaceDesc(MyDDSurface *s, MY_DDSURFACEDESC *pDesc)
{
    if (!pDesc || pDesc->dwSize < sizeof(MY_DDSURFACEDESC)) return DDERR_INVALIDPARAMS;
    pDesc->dwFlags  = DDSD_WIDTH | DDSD_HEIGHT | DDSD_PITCH | DDSD_CAPS;
    pDesc->dwWidth  = (DWORD)s->width;
    pDesc->dwHeight = (DWORD)s->height;
    pDesc->lPitch   = s->pitch;
    pDesc->ddsCaps.dwCaps = s->isPrimary ? DDSCAPS_PRIMARYSURFACE
                          : s->isBackBuffer ? DDSCAPS_BACKBUFFER
                          : DDSCAPS_OFFSCREENPLAIN;
    if (s->locked) {
        pDesc->dwFlags   |= DDSD_LPSURFACE;
        pDesc->lpSurface  = (DWORD)(ULONG_PTR)s->pixels;
    }
    return DD_OK;
}

static HRESULT __stdcall DDSurf_Initialize(MyDDSurface *s, void *pDD,
    MY_DDSURFACEDESC *pDesc) { return DDERR_UNSUPPORTED; }

static HRESULT __stdcall DDSurf_IsLost(MyDDSurface *s) { return DD_OK; }

/* ─── Lock ─────────────────────────────────────────────────────────────────── */
static HRESULT __stdcall DDSurf_Lock(MyDDSurface *s, RECT *pRect,
                                      MY_DDSURFACEDESC *pDesc,
                                      DWORD dwFlags, HANDLE hEvent)
{
    if (!pDesc) return DDERR_INVALIDPARAMS;
    if (s->locked) return DDERR_ALREADYLOCKED;

    s->locked = TRUE;
    pDesc->dwFlags  |= DDSD_WIDTH | DDSD_HEIGHT | DDSD_PITCH | DDSD_LPSURFACE;
    pDesc->dwWidth   = (DWORD)s->width;
    pDesc->dwHeight  = (DWORD)s->height;
    pDesc->lPitch    = s->pitch;
    pDesc->lpSurface = (DWORD)(ULONG_PTR)s->pixels;

    if (pRect) {
        /* Sub-rect lock: slide the pointer to the rect origin */
        pDesc->dwWidth   = (DWORD)(pRect->right  - pRect->left);
        pDesc->dwHeight  = (DWORD)(pRect->bottom - pRect->top);
        pDesc->lpSurface = (DWORD)(ULONG_PTR)(
            s->pixels + (size_t)pRect->top * s->pitch + pRect->left);
    }
    return DD_OK;
}

static HRESULT __stdcall DDSurf_ReleaseDC(MyDDSurface *s, HDC hDC)
    { return DD_OK; }
static HRESULT __stdcall DDSurf_Restore(MyDDSurface *s)
    { return DD_OK; }
static HRESULT __stdcall DDSurf_SetClipper(MyDDSurface *s, void *pClipper)
    { return DD_OK; }

static HRESULT __stdcall DDSurf_SetColorKey(MyDDSurface *s,
    DWORD dwFlags, MY_DDCOLORKEY *pCK)
{
    if (!pCK) return DDERR_INVALIDPARAMS;
    if (dwFlags & DDCKEY_SRCBLT) {
        s->hasColorKeySrc  = TRUE;
        s->colorKeySrcLow  = pCK->dwLow;
        s->colorKeySrcHigh = pCK->dwHigh;
        LOG("SetColorKey SRCBLT [%u, %u] on %s surface",
            pCK->dwLow, pCK->dwHigh,
            s->isPrimary ? "primary" : s->isBackBuffer ? "backbuf" : "offscreen");
    }
    return DD_OK;
}

static HRESULT __stdcall DDSurf_SetOverlayPosition(MyDDSurface *s, LONG x, LONG y)
    { return DDERR_UNSUPPORTED; }

/* ─── SetPalette ──────────────────────────────────────────────────────────── */
static HRESULT __stdcall DDSurf_SetPalette(MyDDSurface *s, MyDDPalette *pPal)
{
    if (s->palette)  DDPal_Release(s->palette);
    s->palette = pPal;
    if (pPal) DDPal_AddRef(pPal);
    LOG("SetPalette on %s surface",
        s->isPrimary ? "primary" : s->isBackBuffer ? "backbuf" : "offscreen");
    return DD_OK;
}

/* ─── Unlock ──────────────────────────────────────────────────────────────── */
static HRESULT __stdcall DDSurf_Unlock(MyDDSurface *s, void *pSurfData)
{
    s->locked = FALSE;
    return DD_OK;
}

static HRESULT __stdcall DDSurf_UpdateOverlay(MyDDSurface *s, RECT *sr,
    MyDDSurface *dst, RECT *dr, DWORD f, void *pFX) { return DDERR_UNSUPPORTED; }
static HRESULT __stdcall DDSurf_UpdateOverlayDisplay(MyDDSurface *s, DWORD f)
    { return DDERR_UNSUPPORTED; }
static HRESULT __stdcall DDSurf_UpdateOverlayZOrder(MyDDSurface *s,
    DWORD f, MyDDSurface *ref) { return DDERR_UNSUPPORTED; }

/* ═══════════════════════════════════════════════════════════════════════════
   IDirectDraw  implementation
   ═══════════════════════════════════════════════════════════════════════════ */
static ULONG __stdcall DD_AddRef(MyDDraw *dd)
    { return (ULONG)InterlockedIncrement(&dd->refCount); }

static ULONG __stdcall DD_Release(MyDDraw *dd)
{
    LONG ref = InterlockedDecrement(&dd->refCount);
    if (ref == 0) {
        LOG("IDirectDraw freed");
        if (dd->primarySurface) {
            DDSurf_Release(dd->primarySurface);
            dd->primarySurface = NULL;
        }
        if (dd == g_dd) g_dd = NULL;
        free(dd);
    }
    return (ULONG)ref;
}

static HRESULT __stdcall DD_QueryInterface(MyDDraw *dd, REFIID r, void **ppv)
    { *ppv = dd; DD_AddRef(dd); return DD_OK; }

static HRESULT __stdcall DD_Compact(MyDDraw *dd) { return DD_OK; }

static HRESULT __stdcall DD_CreateClipper(MyDDraw *dd, DWORD dwFlags,
    void **ppClipper, void *pUnk)
    { if (ppClipper) *ppClipper = NULL; return DDERR_UNSUPPORTED; }

/* ─── CreatePalette ───────────────────────────────────────────────────────── */
static HRESULT __stdcall DD_CreatePalette(MyDDraw *dd, DWORD dwFlags,
    PALETTEENTRY *pEntries, MyDDPalette **ppPal, void *pUnk)
{
    if (!ppPal) return DDERR_INVALIDPARAMS;
    MyDDPalette *pal = (MyDDPalette *)calloc(1, sizeof(MyDDPalette));
    if (!pal) return DDERR_OUTOFMEMORY;
    pal->lpVtbl   = g_DDPalVtbl;
    pal->refCount = 1;
    if (pEntries) {
        for (int i = 0; i < 256; i++) {
            pal->entries[i].rgbRed      = pEntries[i].peRed;
            pal->entries[i].rgbGreen    = pEntries[i].peGreen;
            pal->entries[i].rgbBlue     = pEntries[i].peBlue;
            pal->entries[i].rgbReserved = 0;
        }
    }
    *ppPal = pal;
    LOG("CreatePalette flags=%08X", dwFlags);
    return DD_OK;
}

/* ─── CreateSurface ───────────────────────────────────────────────────────── */
static HRESULT __stdcall DD_CreateSurface(MyDDraw *dd, MY_DDSURFACEDESC *pDesc,
    MyDDSurface **ppSurf, void *pUnk)
{
    if (!pDesc || !ppSurf) return DDERR_INVALIDPARAMS;

    DWORD caps = pDesc->ddsCaps.dwCaps;

    /* Determine surface dimensions */
    int w = (int)pDesc->dwWidth;
    int h = (int)pDesc->dwHeight;

    if (caps & DDSCAPS_PRIMARYSURFACE) {
        /* Primary surface inherits display-mode dimensions */
        if (dd->gameWidth  > 0) w = dd->gameWidth;
        if (dd->gameHeight > 0) h = dd->gameHeight;
        if (w <= 0) w = 640;
        if (h <= 0) h = 480;

        MyDDSurface *primary = surf_create(dd, w, h, TRUE, FALSE);
        if (!primary) return DDERR_OUTOFMEMORY;

        /* Automatically create attached back-buffer for flip chains */
        DWORD backCount = pDesc->dwBackBufferCount ? pDesc->dwBackBufferCount : 1;
        if ((caps & DDSCAPS_FLIP) && backCount > 0) {
            MyDDSurface *back = surf_create(dd, w, h, FALSE, TRUE);
            if (!back) { free(primary->pixels); free(primary); return DDERR_OUTOFMEMORY; }
            primary->attachedBackBuffer = back;
        }
        dd->primarySurface = primary;
        *ppSurf = primary;
        LOG("CreateSurface primary %dx%d caps=%08X backCount=%u", w, h, caps, backCount);
        return DD_OK;
    }

    /* Offscreen plain surface – dimensions must be in the desc */
    if (w <= 0 || h <= 0) { w = 1; h = 1; }
    MyDDSurface *surf = surf_create(dd, w, h, FALSE, FALSE);
    if (!surf) return DDERR_OUTOFMEMORY;
    *ppSurf = surf;
    LOG("CreateSurface offscreen %dx%d caps=%08X", w, h, caps);
    return DD_OK;
}

static HRESULT __stdcall DD_DuplicateSurface(MyDDraw *dd,
    MyDDSurface *pSrc, MyDDSurface **ppDup)
{
    /* Return the same surface with an extra ref (simplification for EFZ) */
    if (!pSrc || !ppDup) return DDERR_INVALIDPARAMS;
    DDSurf_AddRef(pSrc);
    *ppDup = pSrc;
    return DD_OK;
}

static HRESULT __stdcall DD_EnumDisplayModes(MyDDraw *dd, DWORD dwFlags,
    MY_DDSURFACEDESC *pDesc, void *pCtx, void *pfn) { return DD_OK; }
static HRESULT __stdcall DD_EnumSurfaces(MyDDraw *dd, DWORD dwFlags,
    MY_DDSURFACEDESC *pDesc, void *pCtx, void *pfn) { return DD_OK; }
static HRESULT __stdcall DD_FlipToGDISurface(MyDDraw *dd) { return DD_OK; }
static HRESULT __stdcall DD_GetCaps(MyDDraw *dd, void *pDDCaps, void *pDDCaps2)
    { return DD_OK; }

static HRESULT __stdcall DD_GetDisplayMode(MyDDraw *dd, MY_DDSURFACEDESC *pDesc)
{
    if (!pDesc) return DDERR_INVALIDPARAMS;
    pDesc->dwFlags  = DDSD_WIDTH | DDSD_HEIGHT | DDSD_PITCH | DDSD_PIXELFORMAT;
    pDesc->dwWidth  = (DWORD)dd->gameWidth;
    pDesc->dwHeight = (DWORD)dd->gameHeight;
    pDesc->lPitch   = dd->gameWidth;
    pDesc->ddpfPixelFormat.dwSize        = sizeof(MY_DDPIXELFORMAT);
    pDesc->ddpfPixelFormat.dwFlags       = 0x00000200; /* DDPF_PALETTEINDEXED8 */
    pDesc->ddpfPixelFormat.dwRGBBitCount = 8;
    return DD_OK;
}

static HRESULT __stdcall DD_GetFourCCCodes(MyDDraw *dd, DWORD *pNum, DWORD *pCodes)
    { return DD_OK; }
static HRESULT __stdcall DD_GetGDISurface(MyDDraw *dd, MyDDSurface **ppSurf)
    { return DDERR_UNSUPPORTED; }
static HRESULT __stdcall DD_GetMonitorFrequency(MyDDraw *dd, DWORD *pFreq)
    { if (pFreq) *pFreq = 60; return DD_OK; }
static HRESULT __stdcall DD_GetScanLine(MyDDraw *dd, DWORD *pSL)
    { if (pSL) *pSL = 0; return DD_OK; }
static HRESULT __stdcall DD_GetVerticalBlankStatus(MyDDraw *dd, BOOL *pbInVB)
    { if (pbInVB) *pbInVB = FALSE; return DD_OK; }
static HRESULT __stdcall DD_Initialize(MyDDraw *dd, GUID *pGUID)
    { return DDERR_UNSUPPORTED; }
static HRESULT __stdcall DD_RestoreDisplayMode(MyDDraw *dd)
    { LOG("RestoreDisplayMode"); return DD_OK; }

/* ─── SetCooperativeLevel ─────────────────────────────────────────────────── */
static HRESULT __stdcall DD_SetCooperativeLevel(MyDDraw *dd, HWND hWnd, DWORD dwFlags)
{
    LOG("SetCooperativeLevel hWnd=%p flags=%08X", (void *)hWnd, dwFlags);
    dd->hWnd = hWnd;
    g_dd     = dd;

    /* Sub-class the game's WndProc so we can intercept WM_PAINT / activation */
    if (hWnd && !dd->origWndProc) {
        dd->origWndProc = (WNDPROC)SetWindowLongPtrA(
            hWnd, GWLP_WNDPROC, (LONG_PTR)WrapperWndProc);
        LOG("WndProc hooked (orig=%p)", (void *)dd->origWndProc);
    }

    /* Install low-level keyboard hook for reliable F11 interception */
    if (!g_kbHook) {
        g_kbHook = SetWindowsHookExA(WH_KEYBOARD_LL, LLKeyboardProc, g_hInst, 0);
        LOG("LL keyboard hook installed (%p)", (void *)g_kbHook);
    }
    /* Window layout is applied once we know the resolution (SetDisplayMode) */
    if (dd->gameWidth > 0)
        set_windowed_mode(dd);

    return DD_OK;
}

/* ─── SetDisplayMode ──────────────────────────────────────────────────────── */
static HRESULT __stdcall DD_SetDisplayMode(MyDDraw *dd,
    DWORD dwWidth, DWORD dwHeight, DWORD dwBPP)
{
    dd->gameWidth  = (int)dwWidth;
    dd->gameHeight = (int)dwHeight;
    LOG("SetDisplayMode %ux%ux%u", dwWidth, dwHeight, dwBPP);

    if (dd->hWnd)
        set_windowed_mode(dd);

    return DD_OK;
}

static HRESULT __stdcall DD_WaitForVerticalBlank(MyDDraw *dd,
    DWORD dwFlags, HANDLE hEvent)
{
    if (dwFlags == 1) Sleep(1);   /* DDWAITVB_BLOCKBEGIN: yield briefly */
    return DD_OK;
}

/*
 * IDirectDraw vtable  –  23 entries, exact SDK order.
 *
 * Offsets the EFZ games are known to call:
 *   +  8  Release
 *   + 20  CreatePalette
 *   + 24  CreateSurface
 *   + 76  RestoreDisplayMode
 *   + 80  SetCooperativeLevel
 *   + 84  SetDisplayMode
 */
static void *g_DDVtbl[] = {
    (void *)DD_QueryInterface,          /*  0  +  0 */
    (void *)DD_AddRef,                  /*  1  +  4 */
    (void *)DD_Release,                 /*  2  +  8 */
    (void *)DD_Compact,                 /*  3  + 12 */
    (void *)DD_CreateClipper,           /*  4  + 16 */
    (void *)DD_CreatePalette,           /*  5  + 20 */
    (void *)DD_CreateSurface,           /*  6  + 24 */
    (void *)DD_DuplicateSurface,        /*  7  + 28 */
    (void *)DD_EnumDisplayModes,        /*  8  + 32 */
    (void *)DD_EnumSurfaces,            /*  9  + 36 */
    (void *)DD_FlipToGDISurface,        /* 10  + 40 */
    (void *)DD_GetCaps,                 /* 11  + 44 */
    (void *)DD_GetDisplayMode,          /* 12  + 48 */
    (void *)DD_GetFourCCCodes,          /* 13  + 52 */
    (void *)DD_GetGDISurface,           /* 14  + 56 */
    (void *)DD_GetMonitorFrequency,     /* 15  + 60 */
    (void *)DD_GetScanLine,             /* 16  + 64 */
    (void *)DD_GetVerticalBlankStatus,  /* 17  + 68 */
    (void *)DD_Initialize,              /* 18  + 72 */
    (void *)DD_RestoreDisplayMode,      /* 19  + 76 */
    (void *)DD_SetCooperativeLevel,     /* 20  + 80 */
    (void *)DD_SetDisplayMode,          /* 21  + 84 */
    (void *)DD_WaitForVerticalBlank,    /* 22  + 88 */
};

/* ═══════════════════════════════════════════════════════════════════════════
   Exported API
   ═══════════════════════════════════════════════════════════════════════════ */

__declspec(dllexport)
HRESULT __stdcall DirectDrawCreate(GUID *lpGUID, void **lplpDD, void *pUnkOuter)
{
    LOG("DirectDrawCreate called (pid=%u)", GetCurrentProcessId());
    if (!lplpDD) return DDERR_INVALIDPARAMS;

    hook_dinput_iat();

    MyDDraw *dd = (MyDDraw *)calloc(1, sizeof(MyDDraw));
    if (!dd) return DDERR_OUTOFMEMORY;
    dd->lpVtbl   = g_DDVtbl;
    dd->refCount = 1;
    g_dd  = dd;
    *lplpDD = dd;

    LOG("DirectDrawCreate OK → %p", (void *)dd);
    return DD_OK;
}

__declspec(dllexport)
HRESULT __stdcall DirectDrawCreateEx(GUID *lpGUID, void **lplpDD,
    REFIID iid, void *pUnkOuter)
    { return DirectDrawCreate(lpGUID, lplpDD, pUnkOuter); }

__declspec(dllexport)
HRESULT __stdcall DirectDrawCreateClipper(DWORD dwFlags,
    void **ppClipper, void *pUnkOuter)
    { if (ppClipper) *ppClipper = NULL; return DDERR_UNSUPPORTED; }

__declspec(dllexport)
HRESULT __stdcall DirectDrawEnumerateA(void *pfn, void *pCtx)  { return DD_OK; }
__declspec(dllexport)
HRESULT __stdcall DirectDrawEnumerateW(void *pfn, void *pCtx)  { return DD_OK; }
__declspec(dllexport)
HRESULT __stdcall DirectDrawEnumerateExA(void *pfn, void *pCtx, DWORD f) { return DD_OK; }
__declspec(dllexport)
HRESULT __stdcall DirectDrawEnumerateExW(void *pfn, void *pCtx, DWORD f) { return DD_OK; }

__declspec(dllexport) HRESULT __stdcall DllCanUnloadNow(void) { return 1 /*S_FALSE*/; }
__declspec(dllexport)
HRESULT __stdcall DllGetClassObject(REFIID rclsid, REFIID riid, void **ppv)
    { if (ppv) *ppv = NULL; return 0x80040111L /*CLASS_E_CLASSNOTAVAILABLE*/; }

/* ═══════════════════════════════════════════════════════════════════════════
   DllMain
   ═══════════════════════════════════════════════════════════════════════════ */
BOOL WINAPI DllMain(HINSTANCE hInstDLL, DWORD fdwReason, LPVOID lpvReserved)
{
    switch (fdwReason) {
        case DLL_PROCESS_ATTACH:
            g_hInst = hInstDLL;
            DisableThreadLibraryCalls(hInstDLL);
            log_init();
            LOG("DLL attached (pid=%u)", GetCurrentProcessId());
            break;
        case DLL_PROCESS_DETACH:
            if (g_kbHook) {
                UnhookWindowsHookEx(g_kbHook);
                g_kbHook = NULL;
            }
            if (g_log) {
                LOG("DLL detached");
                fclose(g_log);
                g_log = NULL;
            }
            if (g_logCSInit) {
                DeleteCriticalSection(&g_logCS);
                g_logCSInit = FALSE;
            }
            break;
    }
    return TRUE;
}
