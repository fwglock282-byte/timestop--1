// out of order arrange packets module
#include "iup.h"
#include "common.h"
#define NAME "ood"
// keep a picked packet at most for KEEP_TURNS_MAX steps, or if there's no following
// one, it will just be sent

static Ihandle* inboundCheckbox;
static Ihandle* outboundCheckbox;
static Ihandle* chanceInput;
static Ihandle* turnsInput;
static Ihandle* dropdownMode;

static volatile short oodEnabled = 0;
static volatile short oodInbound = 1;
static volatile short oodOutbound = 1;
static volatile int reorders = 10;
static volatile int chance = 1000;
static PacketNode* oodPacket = NULL;
static int giveUpCnt;

static int dropdownChangedCallback(Ihandle* self, char* t, int i, int v) {
    dropdownMode = i;
    return IUP_DEFAULT;
}


static Ihandle* oodSetupUI() {
    Ihandle* dropdown = IupList(NULL);
    IupSetAttribute(dropdown, "DROPDOWN", "YES");
    IupSetAttribute(dropdown, "1", "NORMAL");
    IupSetAttribute(dropdown, "2", "REVERSE");

    IupSetCallback(dropdown, "ACTION", (Icallback)dropdownChangedCallback);

    Ihandle* oodControlsBox = IupHbox(
        inboundCheckbox = IupToggle("Inbound", NULL),
        outboundCheckbox = IupToggle("Outbound", NULL),
        IupLabel("(%):"),
        chanceInput = IupText(NULL),
        IupLabel("Turns(cnt):"),
        turnsInput = IupText(NULL),
        dropdown,
        NULL
    );

    IupSetAttribute(chanceInput, "VISIBLECOLUMNS", "4");
    IupSetAttribute(chanceInput, "VALUE", "100");
    IupSetCallback(chanceInput, "VALUECHANGED_CB", uiSyncChance);
    IupSetAttribute(chanceInput, SYNCED_VALUE, (char*)&chance);

    //turns input
    IupSetAttribute(turnsInput, "VISIBLECOLUMNS", "4");
    IupSetAttribute(turnsInput, "VALUE", "10");
    IupSetCallback(turnsInput, "VALUECHANGED_CB", uiSyncInteger);
    IupSetAttribute(turnsInput, SYNCED_VALUE, (char*)&reorders);
    IupSetAttribute(turnsInput, INTEGER_MAX, "9999999999");
    IupSetAttribute(turnsInput, INTEGER_MIN, "0");

    IupSetCallback(inboundCheckbox, "ACTION", (Icallback)uiSyncToggle);
    IupSetAttribute(inboundCheckbox, SYNCED_VALUE, (char*)&oodInbound);
    IupSetCallback(outboundCheckbox, "ACTION", (Icallback)uiSyncToggle);
    IupSetAttribute(outboundCheckbox, SYNCED_VALUE, (char*)&oodOutbound);

    // enable by default to avoid confusing
    IupSetAttribute(inboundCheckbox, "VALUE", "ON");
    IupSetAttribute(outboundCheckbox, "VALUE", "ON");

    if (parameterized) {
        setFromParameter(inboundCheckbox, "VALUE", NAME"-inbound");
        setFromParameter(outboundCheckbox, "VALUE", NAME"-outbound");
        setFromParameter(chanceInput, "VALUE", NAME"-chance");
    }

    return oodControlsBox;
}

static void oodStartUp() {
    LOG("ood enabled");
    giveUpCnt = reorders;
    // assert on the issue that repeatly enable/disable abort the program
    assert(oodPacket == NULL);
}

static void oodCloseDown(PacketNode* head, PacketNode* tail) {
    UNREFERENCED_PARAMETER(tail);
    LOG("ood disabled");
    if (oodPacket != NULL) {
        insertAfter(oodPacket, head);
        oodPacket = NULL; // ! need to empty the ood packet
    }
}

// find the next packet fits the direction check or null
static PacketNode* nextCorrectDirectionNode(PacketNode* p) {
    if (p == NULL) {
        return NULL;
    }

    do {
        p = p->next;
    } while (p->next != NULL && !checkDirection(p->addr.Outbound, oodInbound, oodOutbound));

    return p->next == NULL ? NULL : p;
}

static void REVERSEMODE(PacketNode* a, PacketNode* b) {
    assert(a->next && a->prev && b->next && b->prev); 
    assert(b != a); 
    if (b->next == a) {
        b->prev->next = a;
        a->next->prev = b;
        b->next = a->next;
        a->prev = b->prev;
        b->prev = a;
        a->next = b;
    }
    else {
        PacketNode* pa = b->prev,
            * na = b->next,
            * pb = a->prev,
            * nb = a->next;
        pa->next = na->prev = a;
        a->prev = pa;
        a->next = na;
        pb->next = nb->prev = b;
        b->prev = pb;
        b->next = nb;
    }
}

static void NORMALMODE(PacketNode* a, PacketNode* b) {
    assert(a->prev && a->next && b->prev && b->next);
    assert(a != b);
    if (a->next == b) {
        a->prev->next = b;
        b->next->prev = a;
        a->next = b->next;
        b->prev = a->prev;
        a->prev = b;
        b->next = a;
    }
    else {
        PacketNode* pa = a->prev,
            * na = a->next,
            * pb = b->prev,
            * nb = b->next;
        pa->next = na->prev = b;
        b->prev = pa;
        b->next = na;
        pb->next = nb->prev = a;
        a->prev = pb;
        a->next = nb;
    }
}

static short oodProcess(PacketNode* head, PacketNode* tail) {
    if (oodPacket != NULL) {
        if (!isListEmpty() || --giveUpCnt == 0) {
            LOG("Ooo sent direction %s, is giveup %s", oodPacket->addr.Outbound ? "OUTBOUND" : "INBOUND", giveUpCnt ? "NO" : "YES");
            insertAfter(oodPacket, head);
            oodPacket = NULL;
            giveUpCnt = reorders + chance;
        } 
    }
    else if (!isListEmpty()) {
        PacketNode* pac = head->next;
        if (pac->next == tail) {
            if (checkDirection(pac->addr.Outbound, oodInbound, oodOutbound) && calcChance(chance)) {
                oodPacket = popNode(pac);
                LOG("Ooo picked packet w/ chance %.1f%%, direction %s", chance / 100.0, pac->addr.Outbound ? "OUTBOUND" : "INBOUND");
                return TRUE;
            }
        }
        else if (calcChance(chance)) {
            PacketNode* first = head, * second;
            if (dropdownMode == 1) {
                do {
                    first = nextCorrectDirectionNode(first);
                    second = nextCorrectDirectionNode(first);
                    if (first && second && calcChance(chance)) {
                        NORMALMODE(first, second);
                        LOG("Multiple packets OOD swapping");
                    }
                    else {
                        first = second;
                    }
                } while (first && second);
            }
            if (dropdownMode == 2) {
                do {
                    first = nextCorrectDirectionNode(first);
                    second = nextCorrectDirectionNode(first);
                    if (first && second && calcChance(chance)) {
                        REVERSEMODE(second, first);
                        LOG("Multiple packets OOD swapping");
                    }
                    else {
                        first = second;
                    }
                } while (first && second);
            }
            return TRUE;
        }
    }

    return FALSE;
}

Module oodModule = {
    "Ood",
    NAME,
    (short*)&oodEnabled,
    oodSetupUI,
    oodStartUp,
    oodCloseDown,
    oodProcess,
    0, 0, NULL
};
