/*****************************************************************************
 *
 * MODULE:             JN-AN-1171
 *
 * COMPONENT:          app_zcl_task.c
 *
 * DESCRIPTION:        ZLL Demo:
 *
 ****************************************************************************
 *
 * This software is owned by NXP B.V. and/or its supplier and is protected
 * under applicable copyright laws. All rights are reserved. We grant You,
 * and any third parties, a license to use this software solely and
 * exclusively on NXP products [NXP Microcontrollers such as JN5168, JN5164,
 * JN5161, JN5148, JN5142, JN5139].
 * You, and any third parties must reproduce the copyright and warranty notice
 * and any other legend of ownership on each copy or partial copy of the
 * software.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 *
 * Copyright NXP B.V. 2012. All rights reserved
 *
 ***************************************************************************/

/****************************************************************************/
/***        Include files                                                 ***/
/****************************************************************************/

#include <jendefs.h>
#include <appapi.h>
#include "os.h"
#include "os_gen.h"
#include "pdum_apl.h"
#include "pdum_gen.h"
#include "pdm.h"
#include "dbg.h"
#include "pwrm.h"
#include "zps_gen.h"
#include "zps_apl_af.h"
#include "zps_apl_zdo.h"
#include "zps_apl_aib.h"
#include "zps_apl_zdp.h"

#include "PDM_IDs.h"

#include "app_timer_driver.h"

#include "zcl.h"
#include "zcl_options.h"
#include "zll.h"
#include "zll_commission.h"
#include "commission_endpoint.h"

#include "app_zcl_light_task.h"
#include "zpr_light_node.h"
#include "app_light_calibration.h"
#include "app_temp_sensor.h"
#include "app_common.h"
#include "identify.h"
#include "Groups.h"
#include "Groups_internal.h"

#include "zps_gen.h"

#include "app_events.h"
#include "app_light_interpolation.h"
#include "DriverBulb.h"

#include <string.h>

#ifdef CLD_OTA
    #include "app_ota_client.h"
#endif


#ifdef DEBUG_ZCL
#define TRACE_ZCL   TRUE
#else
#define TRACE_ZCL   FALSE
#endif

#ifdef DEBUG_LIGHT_TASK
#define TRACE_LIGHT_TASK  TRUE
#else
#define TRACE_LIGHT_TASK FALSE
#endif



#define TRACE_PATH  FALSE

/****************************************************************************/
/***        Macro Definitions                                             ***/
/****************************************************************************/

/****************************************************************************/
/***        Type Definitions                                              ***/
/****************************************************************************/

#define ZCL_TICK_TIME           APP_TIME_MS(100)


/****************************************************************************/
/***        Local Function Prototypes                                     ***/
/****************************************************************************/
PRIVATE void APP_ZCL_cbGeneralCallback(tsZCL_CallBackEvent *psEvent);
PRIVATE void APP_ZCL_cbEndpointCallback(tsZCL_CallBackEvent *psEvent);
PRIVATE void APP_ZCL_cbZllCommissionCallback(tsZCL_CallBackEvent *psEvent);




/****************************************************************************/
/***        Exported Variables                                            ***/
/****************************************************************************/
//extern PDM_tsRecordDescriptor sScenesDataPDDesc;
PUBLIC uint32 u32ComputedWhiteMode;

/****************************************************************************/
/***        Local Variables                                               ***/
/****************************************************************************/

PRIVATE tsZLL_CommissionEndpoint sCommissionEndpoint;


/****************************************************************************/
/***        Exported Functions                                            ***/
/****************************************************************************/

/****************************************************************************
 *
 * NAME: APP_ZCL_vInitialise
 *
 * DESCRIPTION:
 * Initialises ZCL related functions
 *
 * RETURNS:
 * void
 *
 ****************************************************************************/
PUBLIC void APP_ZCL_vInitialise(void)
{
    teZCL_Status eZCL_Status;
    unsigned int i;
    PDM_teStatus ePDMStatus;
	uint16 u16ByteRead;

	/* Before doing any light-related things, check to see if we are using
	 * computed white mode. This needs to be done first, because computed
	 * white mode ends up disabling some lights. */
	ePDMStatus = PDM_eReadDataFromRecord(PDM_ID_APP_COMPUTE_WHITE,
				&u32ComputedWhiteMode,
				sizeof(u32ComputedWhiteMode), &u16ByteRead);
	if ((ePDMStatus != PDM_E_STATUS_OK) || (u16ByteRead != sizeof(u32ComputedWhiteMode)))
	{
		/* Failed to load computed white mode from PDM; load default. */
		u32ComputedWhiteMode = COMPUTED_WHITE_NONE;
	}

    /* Initialise ZLL */
    eZCL_Status = eZLL_Initialise(&APP_ZCL_cbGeneralCallback, apduZCL);
    if (eZCL_Status != E_ZCL_SUCCESS)
    {
        DBG_vPrintf(TRACE_ZCL, "\nErr: eZLL_Initialise:%d", eZCL_Status);
    }

    /* Start the tick timer */
    OS_eStartSWTimer(APP_TickTimer, ZCL_TICK_TIME, NULL);

    for (i = 0; i < u8App_GetNumberOfDevices(); i++)
    {
    	psApp_GetDeviceRecord(i)->u64IEEEAddr = *((uint64*)pvAppApiGetMacAddrLocation());
    }

    /* Register Commission EndPoint */
    eZCL_Status = eApp_ZLL_RegisterEndpoint(&APP_ZCL_cbEndpointCallback,&sCommissionEndpoint);
    if (eZCL_Status != E_ZCL_SUCCESS)
    {
            DBG_vPrintf(TRACE_ZCL, "Error: eZLL_RegisterCommissionEndPoint:%d\r\n", eZCL_Status);
    }

#ifdef CLD_COLOUR_CONTROL
    for (i = 0; i < NUM_RGB_LIGHTS; i++)
    {
    	DBG_vPrintf(TRACE_LIGHT_TASK, "Capabilities %04x\n", sLightRGB[i].sColourControlServerCluster.u16ColourCapabilities);
    }
#endif

    #ifdef CLD_LEVEL_CONTROL
    	if (u32ComputedWhiteMode == COMPUTED_WHITE_NONE)
    	{
			for (i = 0; i < NUM_MONO_LIGHTS; i++)
			{
				sLightMono[i].sLevelControlServerCluster.u8CurrentLevel = 0xFE;
			}
    	}
    	for (i = 0; i < NUM_RGB_LIGHTS; i++)
		{
			sLightRGB[i].sLevelControlServerCluster.u8CurrentLevel = 0xFE;
		}
    #endif

    if (u32ComputedWhiteMode == COMPUTED_WHITE_NONE)
    {
		for (i = 0; i < NUM_MONO_LIGHTS; i++)
		{
			sLightMono[i].sOnOffServerCluster.bOnOff = TRUE;
		}
    }
	for (i = 0; i < NUM_RGB_LIGHTS; i++)
	{
		sLightRGB[i].sOnOffServerCluster.bOnOff = TRUE;
	}

    vAPP_ZCL_DeviceSpecific_Init();

#ifdef CLD_OTA
    vAppInitOTA();
#endif

}


/****************************************************************************
 *
 * NAME: APP_ZCL_vSetIdentifyTime
 *
 * DESCRIPTION:
 * Sets the remaining time in the identify cluster
 *
 * PARAMETERS:
 * bAllEndpoints: Pass TRUE to set remaining time for all endpoints, pass FALSE
 * to set remaining time for a single endpoint.
 * u8Endpoint: Endpoint whose remaining time will be set
 * u16Time: Remaining time for identify
 *
 * RETURNS:
 * void
 *
 ****************************************************************************/
PUBLIC void APP_ZCL_vSetIdentifyTime(bool_t bAllEndpoints, uint8 u8Endpoint, uint16 u16Time)
{
	uint8 u8Index;
	bool_t bIsRGB;

	if (bAllEndpoints)
	{
		/* Set remaining time for all endpoints */
		if (u32ComputedWhiteMode == COMPUTED_WHITE_NONE)
		{
			for (u8Index = 0; u8Index < NUM_MONO_LIGHTS; u8Index++)
			{
				sLightMono[u8Index].sIdentifyServerCluster.u16IdentifyTime = u16Time;
			}
		}
		for (u8Index = 0; u8Index < NUM_RGB_LIGHTS; u8Index++)
		{
			sLightRGB[u8Index].sIdentifyServerCluster.u16IdentifyTime = u16Time;
		}
	}
	else
	{
		/* Set remaining time for a single endpoint */
		if (!bEndPointToNum(u8Endpoint, &bIsRGB, &u8Index))
		{
			return;
		}
		if (bIsRGB)
		{
			sLightRGB[u8Index].sIdentifyServerCluster.u16IdentifyTime = u16Time;
		}
		else if (u32ComputedWhiteMode == COMPUTED_WHITE_NONE)
		{
			sLightMono[u8Index].sIdentifyServerCluster.u16IdentifyTime = u16Time;
		}
	}
}

/****************************************************************************
 *
 * NAME: Tick_Task
 *
 * DESCRIPTION:
 * Task kicked by the tick timer
 *
 * RETURNS:
 * void
 *
 ****************************************************************************/
OS_TASK(Tick_Task)
{

    static uint32 u32Tick10ms = 9;
    static uint32 u32Tick1Sec = 99;

    tsZCL_CallBackEvent sCallBackEvent;
    uint8 i;

    OS_eContinueSWTimer(APP_TickTimer, /*TEN_HZ_TICK_TIME*/APP_TIME_MS(10), NULL);

    u32Tick10ms++;
    u32Tick1Sec++;

    /* Wrap the Tick10ms counter and provide 100ms ticks to cluster */
    if (u32Tick10ms > 9)
    {
        eZLL_Update100mS();
        u32Tick10ms = 0;
        /* Also check whether board is overheating */
        if (!bOverheat && (i16TS_GetTemperature() > TEMPERATURE_OVERHEAT_CUTOFF))
        {
        	bOverheat = TRUE;
        	/* Ensure driver switches off all bulbs */
        	for (i = 0; i < NUM_BULBS; i++)
        	{
        		DriverBulb_vOutput(i);
        	}
        }
        if (bOverheat && (i16TS_GetTemperature() < TEMPERATURE_RESTORE_THRESHOLD))
        {
        	bOverheat = FALSE;
        	/* Restore bulb states */
        	for (i = 0; i < NUM_BULBS; i++)
			{
				DriverBulb_vOutput(i);
			}
        }
    }
#if ( defined CLD_LEVEL_CONTROL) && !(defined MONO_ON_OFF)  /* add in nine 10ms interpolation points */
    else
    {
    	for (i = 0; i < NUM_BULBS; i++)
    	{
    		vLI_CreatePoints(i);
    	}
    }
#endif

#ifdef CLD_OTA
    if (u32Tick1Sec == 82)   /* offset this from the 1 second roll over */
    {
        vRunAppOTAStateMachine();
    }
#endif

    /* Wrap the 1 second  counter and provide 1Hz ticks to cluster */
    if(u32Tick1Sec > 99)
    {
        u32Tick1Sec = 0;
        sCallBackEvent.pZPSevent = NULL;
        sCallBackEvent.eEventType = E_ZCL_CBET_TIMER;
        vZCL_EventHandler(&sCallBackEvent);
    }



}

/****************************************************************************/
/***        Local Functions                                               ***/
/****************************************************************************/
/****************************************************************************
 *
 * NAME: ZCL_Task
 *
 * DESCRIPTION:
 * Main ZCL processing task
 *
 * RETURNS:
 * void
 *
 ****************************************************************************/
OS_TASK(ZCL_Task)
{
    ZPS_tsAfEvent sStackEvent;
    tsZCL_CallBackEvent sCallBackEvent;
    sCallBackEvent.pZPSevent = &sStackEvent;

    /* If there is a stack event to process, pass it on to ZCL */
    sStackEvent.eType = ZPS_EVENT_NONE;
    if (OS_eCollectMessage(APP_msgZpsEvents_ZCL, &sStackEvent) == OS_E_OK)
    {
        DBG_vPrintf(TRACE_ZCL, "\nZCL_Task event:%d",sStackEvent.eType);
        sCallBackEvent.eEventType = E_ZCL_CBET_ZIGBEE_EVENT;
        vZCL_EventHandler(&sCallBackEvent);
    }
}


/****************************************************************************
 *
 * NAME: APP_ZCL_cbGeneralCallback
 *
 * DESCRIPTION:
 * General callback for ZCL events
 *
 * RETURNS:
 * void
 *
 ****************************************************************************/
PRIVATE void APP_ZCL_cbGeneralCallback(tsZCL_CallBackEvent *psEvent)
{
#if TRUE == TRACE_ZCL
    switch (psEvent->eEventType)
    {

    case E_ZCL_CBET_LOCK_MUTEX:
        //DBG_vPrintf(TRACE_ZCL, "\nEVT: Lock Mutex\r\n");
        break;

    case E_ZCL_CBET_UNLOCK_MUTEX:
        //DBG_vPrintf(TRACE_ZCL, "\nEVT: Unlock Mutex\r\n");
        break;

    case E_ZCL_CBET_UNHANDLED_EVENT:
        DBG_vPrintf(TRACE_ZCL, "\nEVT: Unhandled Event\r\n");
        break;

    case E_ZCL_CBET_READ_ATTRIBUTES_RESPONSE:
        DBG_vPrintf(TRACE_ZCL, "\nEVT: Read attributes response");
        break;

    case E_ZCL_CBET_READ_REQUEST:
        DBG_vPrintf(TRACE_ZCL, "\nEVT: Read request");
        break;

    case E_ZCL_CBET_DEFAULT_RESPONSE:
        DBG_vPrintf(TRACE_ZCL, "\nEVT: Default response");
        break;

    case E_ZCL_CBET_ERROR:
        DBG_vPrintf(TRACE_ZCL, "\nEVT: Error %d", psEvent->eZCL_Status);
        break;

    case E_ZCL_CBET_TIMER:
        DBG_vPrintf(TRACE_ZCL, "\nEVT: Timer");
        break;

    case E_ZCL_CBET_ZIGBEE_EVENT:
        DBG_vPrintf(TRACE_ZCL, "\nEVT: ZigBee");
        break;

    case E_ZCL_CBET_CLUSTER_CUSTOM:
        DBG_vPrintf(TRACE_ZCL, "\nEP EVT: Custom");
        break;

    default:
        DBG_vPrintf(TRACE_ZCL, "\nInvalid event type");
        break;

    }
#endif

}


/****************************************************************************
 *
 * NAME: APP_ZCL_cbEndpointCallback
 *
 * DESCRIPTION:
 * Endpoint specific callback for ZCL events
 *
 * RETURNS:
 * void
 *
 ****************************************************************************/
PRIVATE void APP_ZCL_cbEndpointCallback(tsZCL_CallBackEvent *psEvent)
{
	uint8 u8Index = 0;
	bool_t bIsRGB = FALSE;
	uint16 u16IdentifyTime;

    #if (defined CLD_COLOUR_CONTROL)  && !(defined DR1221) && !(defined DR1221_Dimic)
        uint8 u8Red, u8Green, u8Blue;
    #endif
    //DBG_vPrintf(TRACE_ZCL, "\nEntering cbZCL_EndpointCallback");

	if (((psEvent->u8EndPoint >= MULTILIGHT_LIGHT_MONO_1_ENDPOINT)
		  && (psEvent->u8EndPoint < (MULTILIGHT_LIGHT_MONO_1_ENDPOINT + NUM_MONO_LIGHTS)))
		 || ((psEvent->u8EndPoint >= MULTILIGHT_LIGHT_RGB_1_ENDPOINT)
		  && (psEvent->u8EndPoint < (MULTILIGHT_LIGHT_RGB_1_ENDPOINT + NUM_RGB_LIGHTS))))
	{
		bEndPointToNum(psEvent->u8EndPoint, &bIsRGB, &u8Index);
	}

    switch (psEvent->eEventType)
    {

    case E_ZCL_CBET_LOCK_MUTEX:
        //OS_eEnterCriticalSection(HA);
        break;

    case E_ZCL_CBET_UNLOCK_MUTEX:
        //OS_eExitCriticalSection(HA);
        break;

    case E_ZCL_CBET_UNHANDLED_EVENT:
        //DBG_vPrintf(TRACE_ZCL, "\nEP EVT: Unhandled event");
        break;

    case E_ZCL_CBET_READ_INDIVIDUAL_ATTRIBUTE_RESPONSE:
        //DBG_vPrintf(TRACE_ZCL, "\nEP EVT: Rd Attr %04x RS %d AS %d", psEvent->uMessage.sIndividualAttributeResponse.u16AttributeEnum, psEvent->uMessage.sIndividualAttributeResponse.psAttributeStatus->eRequestStatus, psEvent->uMessage.sIndividualAttributeResponse.psAttributeStatus->eAttributeStatus);
        break;

    case E_ZCL_CBET_READ_ATTRIBUTES_RESPONSE:
        //DBG_vPrintf(TRACE_ZCL, "\nEP EVT: Read attributes response");
        break;

    case E_ZCL_CBET_READ_REQUEST:
        //DBG_vPrintf(TRACE_ZCL, "\nEP EVT: Read request");
        break;

    case E_ZCL_CBET_DEFAULT_RESPONSE:
        //DBG_vPrintf(TRACE_ZCL, "\nEP EVT: Default response");
        break;

    case E_ZCL_CBET_ERROR:
        //DBG_vPrintf(TRACE_ZCL, "\nEP EVT: Error");
        break;

    case E_ZCL_CBET_TIMER:
        //DBG_vPrintf(TRACE_ZCL, "\nEP EVT: Timer");
        break;

    case E_ZCL_CBET_ZIGBEE_EVENT:
        //DBG_vPrintf(TRACE_ZCL, "\nEP EVT: ZigBee");
        break;

    case E_ZCL_CBET_CLUSTER_CUSTOM:
        DBG_vPrintf(TRACE_ZCL, "\nEP EVT: Custom Cl %04x\n", psEvent->uMessage.sClusterCustomMessage.u16ClusterId);

        switch(psEvent->uMessage.sClusterCustomMessage.u16ClusterId)
        {
            case GENERAL_CLUSTER_ID_ONOFF:
            {

                tsCLD_OnOffCallBackMessage *psCallBackMessage = (tsCLD_OnOffCallBackMessage*)psEvent->uMessage.sClusterCustomMessage.pvCustomData;

                DBG_vPrintf(TRACE_ZCL, " CmdId=%d", psCallBackMessage->u8CommandId);

                switch(psCallBackMessage->u8CommandId)
                {

                    case E_CLD_ONOFF_CMD_OFF_EFFECT:
                        DBG_vPrintf(TRACE_ZCL, "\nOff with effect %d:%d", psCallBackMessage->uMessage.psOffWithEffectRequestPayload->u8EffectId,
                                                                      psCallBackMessage->uMessage.psOffWithEffectRequestPayload->u8EffectVariant);
                        break;

                }

				if (bIsRGB)
				{
					vApp_eCLD_ColourControl_GetRGB(psEvent->u8EndPoint, &u8Red, &u8Green, &u8Blue);
				}
				else
				{
					u8Red = 0;
					u8Green = 0;
					u8Blue = 0;
				}
#if TRACE_LIGHT_TASK
				if (bIsRGB)
				{
					DBG_vPrintf(TRACE_LIGHT_TASK, "\nR %d G %d B %d L %d ",
								u8Red, u8Green, u8Blue, sLightRGB[u8Index].sLevelControlServerCluster.u8CurrentLevel);
					DBG_vPrintf(TRACE_LIGHT_TASK, "Hue %d Sat %d ",
								sLightRGB[u8Index].sColourControlServerCluster.u8CurrentHue,
								sLightRGB[u8Index].sColourControlServerCluster.u8CurrentSaturation);
					DBG_vPrintf(TRACE_LIGHT_TASK, "X %d Y %d ",
								sLightRGB[u8Index].sColourControlServerCluster.u16CurrentX,
								sLightRGB[u8Index].sColourControlServerCluster.u16CurrentY);
					DBG_vPrintf(TRACE_LIGHT_TASK, "M %d On %d OnTime %d OffTime %d",
								sLightRGB[u8Index].sColourControlServerCluster.u8ColourMode,
								sLightRGB[u8Index].sOnOffServerCluster.bOnOff,
								sLightRGB[u8Index].sOnOffServerCluster.u16OnTime,
								sLightRGB[u8Index].sOnOffServerCluster.u16OffWaitTime);
				}
				else
				{
					DBG_vPrintf(TRACE_LIGHT_TASK, "\nL %d On %d OnTime %d OffTime %d",
								sLightMono[u8Index].sLevelControlServerCluster.u8CurrentLevel,
								sLightMono[u8Index].sOnOffServerCluster.bOnOff,
								sLightMono[u8Index].sOnOffServerCluster.u16OnTime,
								sLightMono[u8Index].sOnOffServerCluster.u16OffWaitTime);
				}
#endif

				if (bIsRGB)
				{
					vRGBLight_SetLevels(BULB_NUM_RGB(u8Index),
										sLightRGB[u8Index].sOnOffServerCluster.bOnOff,
										sLightRGB[u8Index].sLevelControlServerCluster.u8CurrentLevel,
										u8Red,
										u8Green,
										u8Blue);
				}
				else if (u32ComputedWhiteMode == COMPUTED_WHITE_NONE)
				{
					vSetBulbState(BULB_NUM_MONO(u8Index),
								  sLightMono[u8Index].sOnOffServerCluster.bOnOff,
								  sLightMono[u8Index].sLevelControlServerCluster.u8CurrentLevel);
				}
            }
            break;
            case GENERAL_CLUSTER_ID_IDENTIFY:
            {
                tsCLD_IdentifyCallBackMessage *psCallBackMessage = (tsCLD_IdentifyCallBackMessage*)psEvent->uMessage.sClusterCustomMessage.pvCustomData;
                if (psCallBackMessage->u8CommandId == E_CLD_IDENTIFY_CMD_TRIGGER_EFFECT) {
                    DBG_vPrintf(TRACE_LIGHT_TASK, "Identify Cust CB %d\n", psCallBackMessage->uMessage.psTriggerEffectRequestPayload->eEffectId);
                    vStartEffect(psEvent->u8EndPoint, psCallBackMessage->uMessage.psTriggerEffectRequestPayload->eEffectId);
                } else if (psCallBackMessage->u8CommandId == E_CLD_IDENTIFY_CMD_IDENTIFY) {
                    DBG_vPrintf(TRACE_PATH, "\nJP E_CLD_IDENTIFY_CMD_IDENTIFY");
                    APP_vHandleIdentify(psEvent->u8EndPoint);
                }
            }
            break;

#ifdef CLD_OTA
            case OTA_CLUSTER_ID:
            {
                tsOTA_CallBackMessage *psCallBackMessage = (tsOTA_CallBackMessage *)psEvent->uMessage.sClusterCustomMessage.pvCustomData;
                vHandleAppOtaClient(psCallBackMessage);
            }
            break;
#endif

        }
        break;

    case E_ZCL_CBET_WRITE_INDIVIDUAL_ATTRIBUTE:
        DBG_vPrintf(TRACE_ZCL, "\nEP EVT: Write Individual Attribute");
        break;

    case E_ZCL_CBET_CLUSTER_UPDATE:

        if (psEvent->psClusterInstance->psClusterDefinition->u16ClusterEnum == GENERAL_CLUSTER_ID_SCENES)
        {

        }
        else if (psEvent->psClusterInstance->psClusterDefinition->u16ClusterEnum == GENERAL_CLUSTER_ID_IDENTIFY)
        {
            APP_vHandleIdentify(psEvent->u8EndPoint);
        }
        else
        {
        	u16IdentifyTime = 0;
        	if (bIsRGB)
        	{
        		u16IdentifyTime = sLightRGB[u8Index].sIdentifyServerCluster.u16IdentifyTime;
        	}
        	else if (u32ComputedWhiteMode == COMPUTED_WHITE_NONE)
        	{
        		u16IdentifyTime = sLightMono[u8Index].sIdentifyServerCluster.u16IdentifyTime;
        	}
            if (u16IdentifyTime == 0)
            {
				/*
				 * If not identifying then do the light
				 */
                //DBG_vPrintf(TRACE_PATH, "\nPath 2");
            	if (bIsRGB)
				{
					vApp_eCLD_ColourControl_GetRGB(psEvent->u8EndPoint, &u8Red, &u8Green, &u8Blue);
				}
				else
				{
					u8Red = 0;
					u8Green = 0;
					u8Blue = 0;
				}
#if TRACE_LIGHT_TASK
            	if (bIsRGB)
				{
					DBG_vPrintf(TRACE_LIGHT_TASK, "\nR %d G %d B %d L %d ",
								u8Red, u8Green, u8Blue, sLightRGB[u8Index].sLevelControlServerCluster.u8CurrentLevel);
					DBG_vPrintf(TRACE_LIGHT_TASK, "Hue %d Sat %d ",
								sLightRGB[u8Index].sColourControlServerCluster.u8CurrentHue,
								sLightRGB[u8Index].sColourControlServerCluster.u8CurrentSaturation);
					DBG_vPrintf(TRACE_LIGHT_TASK, "X %d Y %d ",
								sLightRGB[u8Index].sColourControlServerCluster.u16CurrentX,
								sLightRGB[u8Index].sColourControlServerCluster.u16CurrentY);
					DBG_vPrintf(TRACE_LIGHT_TASK, "M %d On %d OnTime %d OffTime %d",
								sLightRGB[u8Index].sColourControlServerCluster.u8ColourMode,
								sLightRGB[u8Index].sOnOffServerCluster.bOnOff,
								sLightRGB[u8Index].sOnOffServerCluster.u16OnTime,
								sLightRGB[u8Index].sOnOffServerCluster.u16OffWaitTime);
				}
            	else
				{
					DBG_vPrintf(TRACE_LIGHT_TASK, "\nL %d On %d OnTime %d OffTime %d",
								sLightMono[u8Index].sLevelControlServerCluster.u8CurrentLevel,
								sLightMono[u8Index].sOnOffServerCluster.bOnOff,
								sLightMono[u8Index].sOnOffServerCluster.u16OnTime,
								sLightMono[u8Index].sOnOffServerCluster.u16OffWaitTime);
				}
#endif
                if (bIsRGB)
                {
                    vRGBLight_SetLevels(BULB_NUM_RGB(u8Index),
                    		            sLightRGB[u8Index].sOnOffServerCluster.bOnOff,
                    	                sLightRGB[u8Index].sLevelControlServerCluster.u8CurrentLevel,
                                        u8Red,
                                        u8Green,
                                        u8Blue);
                }
                else if (u32ComputedWhiteMode == COMPUTED_WHITE_NONE)
                {
                	vSetBulbState(BULB_NUM_MONO(u8Index),
                			      sLightMono[u8Index].sOnOffServerCluster.bOnOff,
                			      sLightMono[u8Index].sLevelControlServerCluster.u8CurrentLevel);
                }
            }
        }
        break;

    default:
        DBG_vPrintf(TRACE_ZCL, "\nEP EVT: Invalid evt type 0x%x", (uint8)psEvent->eEventType);
        break;

    }


    if (psEvent->eEventType == E_ZCL_CBET_CLUSTER_CUSTOM)
    {
        if (psEvent->uMessage.sClusterCustomMessage.u16ClusterId == ZLL_CLUSTER_ID_COMMISSIONING)
        {
            APP_ZCL_cbZllCommissionCallback(psEvent);
        }
    }
}

/****************************************************************************
 *
 * NAME: APP_ZCL_cbZllCommissionCallback
 *
 * DESCRIPTION:
 * Endpoint specific callback for ZLL commissioning events
 *
 * RETURNS:
 * void
 *
 ****************************************************************************/
PRIVATE void APP_ZCL_cbZllCommissionCallback(tsZCL_CallBackEvent *psEvent)
{
    APP_CommissionEvent sEvent;
    sEvent.eType = APP_E_COMMISSION_MSG;
    sEvent.u8Lqi = psEvent->pZPSevent->uEvent.sApsInterPanDataIndEvent.u8LinkQuality;
    sEvent.sZllMessage.eCommand = ((tsCLD_ZllCommissionCustomDataStructure*)psEvent->psClusterInstance->pvEndPointCustomStructPtr)->sCallBackMessage.u8CommandId;
    sEvent.sZllMessage.sSrcAddr = ((tsCLD_ZllCommissionCustomDataStructure*)psEvent->psClusterInstance->pvEndPointCustomStructPtr)->sRxInterPanAddr.sSrcAddr;
    memcpy(&sEvent.sZllMessage.uPayload,
            (((tsCLD_ZllCommissionCustomDataStructure*)psEvent->psClusterInstance->pvEndPointCustomStructPtr)->sCallBackMessage.uMessage.psScanRspPayload),
            sizeof(tsZllPayloads));

    OS_ePostMessage(APP_CommissionEvents, &sEvent);
}

/****************************************************************************/
/***        END OF FILE                                                   ***/
/****************************************************************************/
