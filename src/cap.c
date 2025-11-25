#include <windows.h>
#include <mmsystem.h>
#include <stdlib.h>
#include <time.h>
#include "iup.h"
#include "common.h"

#define NAME "bandwidth"
#define BANDWIDTH_MIN "0"
#define BANDWIDTH_MAX "99999"
#define BANDWIDTH_DEFAULT 300
#define BUFFER_MS_DEFAULT 1000
#define BUFFER_MS_MAX 9999999
#define KEEP_AT_MOST 5000

static Ihandle* capInboundCheckbox, * capOutboundCheckbox, * kbpsInput, * kbBufMsInput;

static volatile short capEnabled = 0;
static volatile short capInbound = 1;
static volatile short capOutbound = 1;
static volatile int kbps = BANDWIDTH_DEFAULT;
static volatile int kbBufMs = BUFFER_MS_DEFAULT;

static PacketNode capHeadNode = { 0 }, capTailNode = { 0 };
static PacketNode* bufHead = &capHeadNode, * bufTail = &capTailNode;
static int bufSize = 0;
static int bufSizeBytes = 0;


static DWORD timeWindowStartMs = 0;
static int bytesUsedInWindow = 0;

static INLINE_FUNCTION short isCapBufEmpty() {
    short ret = bufHead->next == bufTail;
    if (ret) assert(bufSize == 0);
    return ret;
}

static Ihandle* capSetupUI() {
    Ihandle* capControlsBox = IupHbox(
        IupLabel(""),
        IupFill(),
        capInboundCheckbox = IupToggle("Inbound", NULL),
        capOutboundCheckbox = IupToggle("Outbound", NULL),
        IupLabel("Bandwidth Cap(kbps):"),
        kbpsInput = IupText(NULL),
        IupLabel("Buffer(ms):"),
        kbBufMsInput = IupText(NULL),
        NULL
    );


    IupSetAttribute(kbpsInput, "VISIBLECOLUMNS", "4");
    IupSetAttribute(kbpsInput, "VALUE", STR(BANDWIDTH_DEFAULT));
    IupSetCallback(kbpsInput, "VALUECHANGED_CB", uiSyncInteger);
    IupSetAttribute(kbpsInput, SYNCED_VALUE, (char*)&kbps);
    IupSetAttribute(kbpsInput, INTEGER_MAX, "9999999");
    IupSetAttribute(kbpsInput, INTEGER_MIN, BANDWIDTH_MIN);

    IupSetAttribute(kbBufMsInput, "VISIBLECOLUMNS", "4");
    IupSetAttribute(kbBufMsInput, "VALUE", STR(BUFFER_MS_DEFAULT));
    IupSetCallback(kbBufMsInput, "VALUECHANGED_CB", uiSyncInteger);
    IupSetAttribute(kbBufMsInput, SYNCED_VALUE, (char*)&kbBufMs);
    IupSetAttribute(kbBufMsInput, INTEGER_MAX, STR(BUFFER_MS_MAX));
    IupSetAttribute(kbBufMsInput, INTEGER_MIN, "0");

    IupSetCallback(capInboundCheckbox, "ACTION", (Icallback)uiSyncToggle);
    IupSetAttribute(capInboundCheckbox, SYNCED_VALUE, (char*)&capInbound);
    IupSetCallback(capOutboundCheckbox, "ACTION", (Icallback)uiSyncToggle);
    IupSetAttribute(capOutboundCheckbox, SYNCED_VALUE, (char*)&capOutbound);

    IupSetAttribute(capInboundCheckbox, "VALUE", "ON");
    IupSetAttribute(capOutboundCheckbox, "VALUE", "ON");

    if (parameterized) {
        setFromParameter(capInboundCheckbox, "VALUE", NAME"-inbound");
        setFromParameter(capOutboundCheckbox, "VALUE", NAME"-outbound");
        setFromParameter(kbpsInput, "VALUE", NAME"-kbps");
        setFromParameter(kbBufMsInput, "VALUE", NAME"-buf-ms");
    }

    return capControlsBox;
}

static void capStartUp() {
    if (bufHead->next == NULL && bufTail->next == NULL) {
        bufHead->next = bufTail;
        bufTail->prev = bufHead;
        bufSize = 0;
    }

    startTimePeriod();
    bufSize = 0;
    bufSizeBytes = 0;
    timeWindowStartMs = timeGetTime();
    bytesUsedInWindow = 0;
}

static void capCloseDown(PacketNode* head, PacketNode* tail) {
    PacketNode* oldLast = tail->prev;
    UNREFERENCED_PARAMETER(head);

    LOG("Closing down bandwidth cap, flushing %d packets", bufSize);

    while (!isCapBufEmpty()) {
        PacketNode* packet = popNode(bufHead->next);
        insertAfter(packet, oldLast);
        bufSizeBytes -= packet->packetLen;
        --bufSize;
    }

    endTimePeriod();
}

static short capProcess(PacketNode* head, PacketNode* tail) {
    PacketNode* next = tail->prev;


    while (next != head) {
        PacketNode* current = next;
        next = next->prev;

        if (checkDirection(current->addr.Outbound, capInbound, capOutbound)) {
            int maxBufferBytes = (kbBufMs * kbps) / 8;

            if (bufSize >= KEEP_AT_MOST || (bufSizeBytes + current->packetLen) > maxBufferBytes) {
                PacketNode* toFree = popNode(current);
                freeNode(toFree);
                InterlockedIncrement16(&capModule.processTriggered);
                IupSetAttribute(capModule.iconHandle, "IMAGE", "doing_icon");
                LOG("Dropped packet - buffer full or exceeds limit");
            }
            else {
                PacketNode* buffered = popNode(current);
                insertAfter(buffered, bufTail->prev);
                bufSizeBytes += buffered->packetLen;
                ++bufSize;
                LOG("Buffered packet, buffer size: %d bytes", bufSizeBytes);
            }
        }
    }

    if (!isCapBufEmpty()) {
        int maxBytesPerWindow = (20 * kbps) / 8;

        while (bufHead->next != bufTail && bytesUsedInWindow < maxBytesPerWindow) {
            PacketNode* packet = popNode(bufHead->next);
            insertAfter(packet, head);

            bufSizeBytes -= packet->packetLen;
            bytesUsedInWindow += packet->packetLen;
            --bufSize;
            LOG("Released packet, bytes used in window: %d/%d", bytesUsedInWindow, maxBytesPerWindow);
        }
    }


    DWORD currentTime = timeGetTime();
    if (currentTime - timeWindowStartMs >= 20) {
        timeWindowStartMs += 20;

        int maxBytesPerWindow = (20 * kbps) / 8;
        if (bytesUsedInWindow <= maxBytesPerWindow) {
            bytesUsedInWindow = 0;
        }
        else {
            bytesUsedInWindow = bytesUsedInWindow - maxBytesPerWindow;
        }
        LOG("Reset window, remaining bytes: %d", bytesUsedInWindow);
    }

    return bufSize > 0;
}

Module capModule = {
    "Cap",
    NAME,
    (short*)&capEnabled,
    capSetupUI,
    capStartUp,
    capCloseDown,
    capProcess,
    0, 0, NULL
};
