// throttling packets
#include "iup.h"
#include "common.h"
#define NAME "throttle"
#define TIME_MIN "0"
#define TIME_MAX "9999"
#define TIME_DEFAULT 3500

static Ihandle *inboundCheckbox, *outboundCheckbox, *chanceInput, *frameInput, *bufferInput, *dropThrottledCheckbox;

static volatile short throttleEnabled = 0,
    throttleInbound = 1,
    throttleOutbound = 1,
    chance = 10000,
    buffer = 0,
    throttleFrame = TIME_DEFAULT,
    dropThrottled = 0; 

static PacketNode throttleHeadNode = {0}, throttleTailNode = {0};
static PacketNode *bufHead = &throttleHeadNode, *bufTail = &throttleTailNode;
static int bufSize = 0;
static DWORD throttleStartTick = 0;

static INLINE_FUNCTION short isBufEmpty() {
    short ret = bufHead->next == bufTail;
    if (ret) assert(bufSize == 0);
    return ret;
}

static Ihandle *throttleSetupUI() {
    Ihandle *throttleControlsBox = IupHbox(
        dropThrottledCheckbox = IupToggle("Drop-Throt", NULL),
        IupLabel("Delay(ms):"),
        frameInput = IupText(NULL),
        inboundCheckbox = IupToggle("Inbound", NULL),
        outboundCheckbox = IupToggle("Outbound", NULL),
        IupLabel("Buffer(buf):"),
        bufferInput = IupText(NULL),
        IupLabel("Chance(%):"),
        chanceInput = IupText(NULL),
        NULL
        );

    IupSetAttribute(chanceInput, "VISIBLECOLUMNS", "4");
    IupSetAttribute(chanceInput, "VALUE", "100");
    IupSetCallback(chanceInput, "VALUECHANGED_CB", uiSyncChance);
    IupSetAttribute(chanceInput, SYNCED_VALUE, (char*)&chance);
    IupSetCallback(inboundCheckbox, "ACTION", (Icallback)uiSyncToggle);
    IupSetAttribute(inboundCheckbox, SYNCED_VALUE, (char*)&throttleInbound);
    IupSetCallback(outboundCheckbox, "ACTION", (Icallback)uiSyncToggle);
    IupSetAttribute(outboundCheckbox, SYNCED_VALUE, (char*)&throttleOutbound);
    IupSetCallback(dropThrottledCheckbox, "ACTION", (Icallback)uiSyncToggle);
    IupSetAttribute(dropThrottledCheckbox, SYNCED_VALUE, (char*)&dropThrottled);

    IupSetAttribute(frameInput, "VISIBLECOLUMNS", "4");
    IupSetAttribute(frameInput, "VALUE", STR(TIME_DEFAULT));
    IupSetCallback(frameInput, "VALUECHANGED_CB", (Icallback)uiSyncInteger);
    IupSetAttribute(frameInput, SYNCED_VALUE, (char*)&throttleFrame);
    IupSetAttribute(frameInput, INTEGER_MAX, TIME_MAX);
    IupSetAttribute(frameInput, INTEGER_MIN, TIME_MIN);

    IupSetAttribute(bufferInput, "VISIBLECOLUMNS", "4");
    IupSetAttribute(bufferInput, "VALUE", STR(5000));
    IupSetCallback(bufferInput, "VALUECHANGED_CB", (Icallback)uiSyncInteger);
    IupSetAttribute(bufferInput, SYNCED_VALUE, (char*)&buffer);
    IupSetAttribute(bufferInput, INTEGER_MAX, 99999);
    IupSetAttribute(bufferInput, INTEGER_MIN, 0);

    IupSetAttribute(inboundCheckbox, "VALUE", "ON");
    IupSetAttribute(outboundCheckbox, "VALUE", "ON");

    if (parameterized) {
        setFromParameter(inboundCheckbox, "VALUE", NAME"-inbound");
        setFromParameter(outboundCheckbox, "VALUE", NAME"-outbound");
        setFromParameter(chanceInput, "VALUE", NAME"-chance");
        setFromParameter(frameInput, "VALUE", NAME"-frame");
    }

    return throttleControlsBox;
}

static void throttleStartUp() {
    if (bufHead->next == NULL && bufTail->next == NULL) {
        bufHead->next = bufTail;
        bufTail->prev = bufHead;
        bufSize = 0;
    } else {
        assert(isBufEmpty());
    }
    throttleStartTick = 0;
    startTimePeriod();
}

static void clearBufPackets(PacketNode *tail) {
    PacketNode *oldLast = tail->prev;
    while (!isBufEmpty()) {
        insertAfter(popNode(bufTail->prev), oldLast);
        --bufSize;
    }
    throttleStartTick = 0;
}

static void dropBufPackets() {
    while (!isBufEmpty()) {
        freeNode(popNode(bufTail->prev));
        --bufSize;
    }
    throttleStartTick = 0;
}


static void throttleCloseDown(PacketNode *head, PacketNode *tail) {
    UNREFERENCED_PARAMETER(tail);
    UNREFERENCED_PARAMETER(head);
    clearBufPackets(tail);
    endTimePeriod();
}

static short throttleProcess(PacketNode *head, PacketNode *tail) {
    short throttled = FALSE;
    UNREFERENCED_PARAMETER(head);
    if (!throttleStartTick) {
        if (!isListEmpty() && calcChance(chance)) {
            throttleStartTick = timeGetTime();
            throttled = TRUE;
            goto THROTTLE_START;
        }
    } else {
THROTTLE_START:
        {
            PacketNode *pac = tail->prev;
            DWORD currentTick = timeGetTime();
            while (bufSize < buffer && pac != head) {
                if (checkDirection(pac->addr.Outbound, throttleInbound, throttleOutbound)) {
                    insertAfter(popNode(pac), bufHead);
                    ++bufSize;
                    pac = tail->prev;
                } else {
                    pac = pac->prev;
                }
            }

            if (bufSize >= buffer || (currentTick - throttleStartTick > (unsigned int)throttleFrame)) {
                if (dropThrottled) {
                    dropBufPackets();
                } else {
                    clearBufPackets(tail);
                }
            }
        }
    }

    return throttled;
}

Module throttleModule = {
    "Throttle",
    NAME,
    (short*)&throttleEnabled,
    throttleSetupUI,
    throttleStartUp,
    throttleCloseDown,
    throttleProcess,
    // runtime fields
    0, 0, NULL
};