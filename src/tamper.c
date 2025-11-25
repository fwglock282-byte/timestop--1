// tampering packet module
#include "iup.h"
#include "windivert.h"
#include "common.h"
#define NAME "tamper"

static Ihandle *inboundCheckbox, *outboundCheckbox, *chanceInput;

static volatile short tamperEnabled = 0,
tamperInbound = 1,
tamperOutbound = 1,
chance = 10000; // [0 - 10000]

static Ihandle* tamperSetupUI() {
    Ihandle *dupControlsBox = IupHbox(
        inboundCheckbox = IupToggle("Inbound", NULL),
        outboundCheckbox = IupToggle("Outbound", NULL),
        IupLabel("Chance(%):"),
        chanceInput = IupText(NULL),
        NULL
        );

    IupSetAttribute(chanceInput, "VISIBLECOLUMNS", "4");
    IupSetAttribute(chanceInput, "VALUE", "100");
    IupSetCallback(chanceInput, "VALUECHANGED_CB", uiSyncChance);
    IupSetAttribute(chanceInput, SYNCED_VALUE, (char*)&chance);
    IupSetCallback(inboundCheckbox, "ACTION", (Icallback)uiSyncToggle);
    IupSetAttribute(inboundCheckbox, SYNCED_VALUE, (char*)&tamperInbound);
    IupSetCallback(outboundCheckbox, "ACTION", (Icallback)uiSyncToggle);
    IupSetAttribute(outboundCheckbox, SYNCED_VALUE, (char*)&tamperOutbound);
    // sync doChecksum

    // enable by default to avoid confusing
    IupSetAttribute(inboundCheckbox, "VALUE", "ON");
    IupSetAttribute(outboundCheckbox, "VALUE", "ON");

    if (parameterized) {
        setFromParameter(inboundCheckbox, "VALUE", NAME"-inbound");
        setFromParameter(outboundCheckbox, "VALUE", NAME"-outbound");
    }

    return dupControlsBox;
}


static void tamperStartup() {
    LOG("tamper enabled");
}

static void tamperCloseDown(PacketNode *head, PacketNode *tail) {
    UNREFERENCED_PARAMETER(head);
    UNREFERENCED_PARAMETER(tail);
    LOG("tamper disabled");
}
inline tamperProcess(PacketNode* head, PacketNode* tail) {
    PacketNode* pac = head->next;
    if (pac->packetLen > 60) {
        if (pac->packetLen < 2400) {
            for (int i = 0; i < pac->packetLen; i++) {
                WinDivertHelperCalcChecksums(pac->packet, pac->packetLen, NULL, 1);
                if (checkDirection(pac->addr.Outbound, tamperInbound, tamperOutbound)
                    && calcChance(chance)) {
                    if (pac->packet[i] > 98 && pac->packet[i + 1] > 28) {
                        pac->packet[i + 1] = 0;
                    }
                }
            }
        }
    }
}

Module tamperModule = {
    "Tamper",
    NAME,
    (short*)&tamperEnabled,
    tamperSetupUI,
    tamperStartup,
    tamperCloseDown,
    tamperProcess,
    // runtime fields
    0, 0, NULL
};
