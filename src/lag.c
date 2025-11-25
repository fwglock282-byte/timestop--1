#include "iup.h"
#include "common.h"
#define NAME "lag"
#define LAG_MIN "0"
#define LAG_MAX "99999"
#define LAG_DEFAULT 4800
#define BUFFER_DEFAULT 5000
#define FLUSHER_DEFAULT 800

static Ihandle *inboundCheckbox, *outboundCheckbox, *timeInput, *flusherInput, *bufferInput;

static volatile short lagEnabled = 0,
    lagInbound = 1,
    lagOutbound = 1,
    flusher = 0,
    buffer = 0,
    lagTime = LAG_DEFAULT;

static PacketNode lagHeadNode = {0}, lagTailNode = {0};
static PacketNode *bufHead = &lagHeadNode, *bufTail = &lagTailNode;
static int bufSize = 0;

static INLINE_FUNCTION short isBufEmpty() {
    short ret = bufHead->next == bufTail;
    if (ret) assert(bufSize == 0);
    return ret;
}

static Ihandle *lagSetupUI() {
    Ihandle *lagControlsBox = IupHbox(
        IupLabel("Delay(ms):"),
        timeInput = IupText(NULL),
        inboundCheckbox = IupToggle("Inbound", NULL),
        outboundCheckbox = IupToggle("Outbound", NULL),
        IupLabel("Buffer(buf):"),
        bufferInput = IupText(NULL),
        IupLabel("Flusher(cnt):"),
        flusherInput = IupText(NULL),
        NULL
        );

    IupSetAttribute(timeInput, "VISIBLECOLUMNS", "4");
    IupSetAttribute(timeInput, "VALUE", STR(LAG_DEFAULT));
    IupSetCallback(timeInput, "VALUECHANGED_CB", uiSyncInteger);
    IupSetAttribute(timeInput, SYNCED_VALUE, (char*)&lagTime);
    IupSetAttribute(timeInput, INTEGER_MAX, LAG_MAX);
    IupSetAttribute(timeInput, INTEGER_MIN, LAG_MIN);

    IupSetAttribute(bufferInput, "VISIBLECOLUMNS", "4");
    IupSetAttribute(bufferInput, "VALUE", STR(BUFFER_DEFAULT));
    IupSetCallback(bufferInput, "VALUECHANGED_CB", uiSyncInteger);
    IupSetAttribute(bufferInput, SYNCED_VALUE, (char*)&buffer);
    IupSetAttribute(bufferInput, INTEGER_MAX, 99999);
    IupSetAttribute(bufferInput, INTEGER_MIN, 0);

    IupSetAttribute(flusherInput, "VISIBLECOLUMNS", "4");
    IupSetAttribute(flusherInput, "VALUE", STR(FLUSHER_DEFAULT));
    IupSetCallback(flusherInput, "VALUECHANGED_CB", uiSyncInteger);
    IupSetAttribute(flusherInput, SYNCED_VALUE, (char*)&flusher);
    IupSetAttribute(flusherInput, INTEGER_MAX, 99999);
    IupSetAttribute(flusherInput, INTEGER_MIN, 0);

    IupSetCallback(inboundCheckbox, "ACTION", (Icallback)uiSyncToggle);
    IupSetAttribute(inboundCheckbox, SYNCED_VALUE, (char*)&lagInbound);
    IupSetCallback(outboundCheckbox, "ACTION", (Icallback)uiSyncToggle);
    IupSetAttribute(outboundCheckbox, SYNCED_VALUE, (char*)&lagOutbound);

    // enable by default to avoid confusing
    IupSetAttribute(inboundCheckbox, "VALUE", "ON");
    IupSetAttribute(outboundCheckbox, "VALUE", "ON");

    if (parameterized) {
        setFromParameter(inboundCheckbox, "VALUE", NAME"-inbound");
        setFromParameter(outboundCheckbox, "VALUE", NAME"-outbound");
        setFromParameter(timeInput, "VALUE", NAME"-time");
    }

    return lagControlsBox;
}

static void lagStartUp() {
    if (bufHead->next == NULL && bufTail->next == NULL) {
        bufHead->next = bufTail;
        bufTail->prev = bufHead;
        bufSize = 0;
    } else {
        assert(isBufEmpty());
    }
    startTimePeriod();
}

static void lagCloseDown(PacketNode *head, PacketNode *tail) {
    PacketNode *oldLast = tail->prev;
    UNREFERENCED_PARAMETER(head);
    while(!isBufEmpty()) {
        insertAfter(popNode(bufTail->prev), oldLast);
        --bufSize;
    }
    endTimePeriod();
}

static short lagProcess(PacketNode *head, PacketNode *tail) {
    DWORD currentTime = timeGetTime();
    PacketNode *pac = tail->prev;
    while (bufSize < buffer && pac != head) {
        if (checkDirection(pac->addr.Outbound, lagInbound, lagOutbound)) {
            insertAfter(popNode(pac), bufHead)->timestamp = timeGetTime();
            ++bufSize;
            pac = tail->prev;
        } else {
            pac = pac->prev;
        }
    }

    while (!isBufEmpty()) {
        pac = bufTail->prev;
        if (currentTime > pac->timestamp + lagTime) {
            insertAfter(popNode(bufTail->prev), head);
            --bufSize;
        } else {
            break;
        }
    }

    if (bufSize >= buffer) {
        int flushCnt = flusher;
        while (flushCnt-- > 0) {
            insertAfter(popNode(bufTail->prev), head);
            --bufSize;
        }
    }

    return bufSize > 0;
}

Module lagModule = {
    "Lag",
    NAME,
    (short*)&lagEnabled,
    lagSetupUI,
    lagStartUp,
    lagCloseDown,
    lagProcess,
    // runtime fields
    0, 0, NULL
};