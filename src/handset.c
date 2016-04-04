#include <stdio.h>
#include <unistd.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>

#include "handset.h"
#include "busmail.h"
#include "util.h"
#include "ubus.h"
#include "app.h"
#include "stream.h"
#include "event_base.h"
#include "error.h"
#include "connection_init.h"

#include <Api/FpGeneral/ApiFpGeneral.h>
#include <Api/CodecList/ApiCodecList.h>
#include <Api/FpCc/ApiFpCc.h>
#include <Api/FpMm/ApiFpMm.h>
#include <Api/ProdTest/ApiProdTest.h>
#include <Api/FpAudio/ApiFpAudio.h>
#include <Api/RsStandard.h>



static void *dect_bus;




//-------------------------------------------------------------
// Query stack for one registered handset IPUI
static void get_handset_ipui(int handsetId)
{
	ApiFpMmGetHandsetIpuiReqType m = {
		.Primitive = API_FP_MM_GET_HANDSET_IPUI_REQ,
		.TerminalId = handsetId,
	};

	mailProto.send(dect_bus, (uint8_t*) &m, sizeof(m));
}



//-------------------------------------------------------------
// Dect stack responded with the IPUI
// of one registered handset.
static void got_handset_ipui(busmail_t *m)
{
	ApiFpMmGetHandsetIpuiCfmType *resp;
	ApiBasicTermCapsType *basicCaps;
	ApiInfoElementType *ie;
	ApiTermCapsType *caps;
	int i;

	resp = (ApiFpMmGetHandsetIpuiCfmType*) &m->mail_header;

	// Sanity check
	if(resp->Status != RSS_SUCCESS) {
		printf("Error; %s() status %d\n", __func__, resp->Status);
		return;
	}

	/* Search for the handset ID in an our list
	 * for the terminal we just got the ipui for. */
	for(i = 0; i < MAX_NR_HANDSETS &&
			handsets.terminal[i].id != resp->TerminalId; i++);

	// Copy unique address
	memcpy(handsets.terminal[i].ipui, resp->IPUI,
		sizeof(handsets.terminal[0].ipui));

	// Copy list of codecs the handset support
	ie = ApiGetInfoElement((ApiInfoElementType*) resp->InfoElement,
		resp->InfoElementLength, API_IE_CODEC_LIST);
	if (ie && ie->IeLength) {
		handsets.terminal[i].codecs = malloc(ie->IeLength);
		memcpy(handsets.terminal[i].codecs, ie->IeData, ie->IeLength);
	}

	// Terminal capabilites	
	ie = ApiGetInfoElement((ApiInfoElementType*) resp->InfoElement,
		resp->InfoElementLength, API_IE_TERMCAPS);
	if (ie && ie->IeLength) {
		caps = (ApiTermCapsType*) ie->IeData;
		handsets.terminal[i].BasicService = (caps->Byte4f & 2u) ? 
			API_WIDEBAND_SPEECH : API_BASIC_SPEECH;
	}

	ie = ApiGetInfoElement((ApiInfoElementType*) resp->InfoElement,
		resp->InfoElementLength, API_IE_BASIC_TERMCAPS);
	if (ie && ie->IeLength) {
		basicCaps = (ApiBasicTermCapsType*) ie->IeData;
		handsets.terminal[i].BasicService = (basicCaps->Flags0 & 3u) ? 
			API_WIDEBAND_SPEECH : API_BASIC_SPEECH;
	}


	/* Query next handset if we yet
	 * haven't got them all. */
	i++;
	if(i >= handsets.termCount) {
		perhaps_disable_radio();
		ubus_reply_handset_list(0, &handsets);
		printf("Has updated the handset list\n");
	}
	else {
		get_handset_ipui(handsets.terminal[i].id);
	}
}



//-------------------------------------------------------------
// A handset has been deleted from the list of registereds
static int handset_delete_cfm(busmail_t *m)
{
	ApiFpMmDeleteRegistrationCfmType *msgIn;
	int i;

	msgIn = (ApiFpMmDeleteRegistrationCfmType*) &m->mail_header;
	if(msgIn->Status != RSS_SUCCESS) return -1;
	if(!handsets.termCount) return -1;

	for(i = 0; i < MAX_NR_HANDSETS; i++) {
		if(handsets.terminal[i].id == msgIn->TerminalId) {
			free(handsets.terminal[i].codecs);
			memset(&handsets.terminal[i], 0, sizeof(struct terminal_t));
			handsets.termCount--;
			perhaps_disable_radio();
			ubus_send_string("handset", "remove");
			break;
		}
	}

	return 0;
}



//-------------------------------------------------------------
// A new handset has been registered
static int handset_registerd_cfm(busmail_t *m)
{
	ApiFpMmRegistrationCompleteIndType *msgIn;
	ApiBasicTermCapsType *basicCaps;
	ApiTermCapsType *caps;
	ApiInfoElementType *ie;
	int i;

	msgIn = (ApiFpMmRegistrationCompleteIndType*) &m->mail_header;
	if(msgIn->Status != RSS_SUCCESS) return -1;
	if(handsets.termCount == MAX_NR_HANDSETS) return -1;

	/* Find a free slot in the list of registered
	 * handset and add its' properties. */
	for(i = 0; i < MAX_NR_HANDSETS && handsets.terminal[i].id; i++);
	handsets.terminal[i].id = msgIn->TerminalId;

	// Codecs
	ie = ApiGetInfoElement((ApiInfoElementType*) msgIn->InfoElement,
		msgIn->InfoElementLength, API_IE_CODEC_LIST);
	if (ie && ie->IeLength) {
		handsets.terminal[i].codecs = malloc(ie->IeLength);
		memcpy(handsets.terminal[i].codecs, ie->IeData, ie->IeLength);
	}

	// Terminal capabilites	
	ie = ApiGetInfoElement((ApiInfoElementType*) msgIn->InfoElement,
		msgIn->InfoElementLength, API_IE_TERMCAPS);
	if (ie && ie->IeLength) {
		caps = (ApiTermCapsType*) ie->IeData;
		handsets.terminal[i].BasicService = (caps->Byte4f & 2u) ? 
			API_WIDEBAND_SPEECH : API_BASIC_SPEECH;
	}

	ie = ApiGetInfoElement((ApiInfoElementType*) msgIn->InfoElement,
		msgIn->InfoElementLength, API_IE_BASIC_TERMCAPS);
	if (ie && ie->IeLength) {
		basicCaps = (ApiBasicTermCapsType*) ie->IeData;
		handsets.terminal[i].BasicService = (basicCaps->Flags0 & 3u) ? 
			API_WIDEBAND_SPEECH : API_BASIC_SPEECH;
	}
	
	handsets.termCount++;
	ubus_send_string("handset", "add");
	perhaps_disable_radio();

	return 0;
}



//-------------------------------------------------------------
// Query Dect stack for what handsets are registered
int list_handsets(void)
{
	ApiFpMmGetRegistrationCountReqType m = {
		.Primitive = API_FP_MM_GET_REGISTRATION_COUNT_REQ,
		.StartTerminalId = 0
	};

	printf("Querying number of registered handsets\n");
	mailProto.send(dect_bus, (uint8_t*) &m, sizeof(m));

	return 0;
}



//-------------------------------------------------------------
// Dect stack responded whith current
// number of registered handsets.
static void got_registration_count(busmail_t *m)
{
	ApiFpMmGetRegistrationCountCfmType *resp;
	int i;
	
	resp = (ApiFpMmGetRegistrationCountCfmType*) &m->mail_header;
	
	// Sanity check
	if(resp->Status != RSS_SUCCESS) {
		printf("Error; %s() status %d\n", __func__, resp->Status);
		return;
	}
	if(resp->TerminalIdCount > resp->MaxNoHandsets) return;

	for(i = 0; i < MAX_NR_HANDSETS; i++) free(handsets.terminal[i].codecs);
	handsets.termCount = resp->TerminalIdCount;
	memset(handsets.terminal, 0, sizeof(struct terminal_t) * MAX_NR_HANDSETS);
	printf("There are %d registered handsets\n", handsets.termCount);

	for(i = 0 ; i < resp->TerminalIdCount; i++) {
		handsets.terminal[i].id = resp->TerminalId[i];
	}

	/* Get the ipui of the first handset. For some damn
	 * reason we can't to all of them at once. */
	if (handsets.termCount > 0) {
		get_handset_ipui(handsets.terminal[0].id);
	}
	else {
		perhaps_disable_radio();
		ubus_reply_handset_list(0, &handsets);
	}
}



//-------------------------------------------------------------
// Delete a registered handset from Dect stack
int delete_handset(int id) {  
	ApiFpMmDeleteRegistrationReqType m = {
		.Primitive = API_FP_MM_DELETE_REGISTRATION_REQ,
		.TerminalId = id
	};

	mailProto.send(dect_bus, (uint8_t*) &m, sizeof(m));

	return 0;
}


//-------------------------------------------------------------
// Make ALL handsets ring. To make only one of
// them ring Asterisk has to be used, due to such
// a case works like a phone call even though there
// is no audio.
int page_all_handsets(void) {
	ApiFpMmAlertBroadcastReqType m = {
		.Primitive = API_FP_MM_ALERT_BROADCAST_REQ,
		.Signal = API_CC_SIGNAL_ALERT_ON_PATTERN_2
	};

	mailProto.send(dect_bus, (uint8_t*) &m, sizeof(m));

	return 0;
}



//-------------------------------------------------------------
// Handle PP events such as registration
static void handset_handler(packet_t *p)
{
	busmail_t * m = (busmail_t *) &p->data[0];

	if (m->task_id != 1) return;
	
	/* Application command */
	switch (m->mail_header) {
		case API_FP_MM_HANDSET_PRESENT_IND:
			ubus_send_string("handset", "present");
			break;

		case API_FP_MM_GET_REGISTRATION_COUNT_CFM:
			got_registration_count(m);
			break;

		case API_FP_MM_GET_HANDSET_IPUI_CFM:
			got_handset_ipui(m);
			break;

		case API_FP_MM_REGISTRATION_COMPLETE_IND:
			handset_registerd_cfm(m);
			break;

		case API_FP_MM_DELETE_REGISTRATION_CFM:
			handset_delete_cfm(m);
			break;
	}
}




//-------------------------------------------------------------
void handset_init(void * bus)
{
	memset(&handsets, 0, sizeof(handsets));

	dect_bus = bus;
	mailProto.add_handler(bus, handset_handler);
}


