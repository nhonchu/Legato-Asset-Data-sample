//-------------------------------------------------------------------------------------------------
/**
 * @file fridgeTruck.c
 *
 * Sample app simulating a refrigerated truck
 *
 *  Simulated scenario:
 *
 *    Legato app:
 *      When the fan (AC) is operating, the temperature of the truck converges to the 'target temperature'
 *      When temperature reaches the 'target temperature', the fan automatically stops
 *      When the fan is stopped or truck door is opened, truck's temperature converges to the 'outside air temperature'
 *      The truck is posting the fan status (on/off) and door status (opened/closed) on a regular basis (interval.datapush) to AirVantage
 *      The current position of the truck is also pushed to AirVantage
 *      The truck is collecting the current temprature and fan duration on a regular basis (interval.datagen)
 *          this data is timestamped and is pushed to AirVantage as timeserie data (chunk of TIMESERIE_MAX_RECORD)
 *
 *    LED, motor & swicth, connected to IoT card (slot 0) of a mangOH board:
 *      a push button to open/close the truck door - [IoT0, GPIO_1]
 *      a LED to echo the status of the door (opened, closed) - [IoT0, GPIO_2]
 *      a Motor to provide visual feedback of the fan status (operating or not) - [IoT0, GPIO_3]
 *
 *    Rules are defined on AirVantage to perform the following actions automatically:
 *      When the temperature exceeds a limit (e.g. 5°C) then send a command to the truck to Start the Fan
 *      When the fan duration exceeds a limit then send an email to maintenance team
 *
 *    Operation team can perform the following actions on AirVantage:
 *      Send commands to truck to : Start/Stop Fan, Simulate Open/Close door
 *      Change truck settings, e.g. 'target temperature', 'outside air temperature', interval.datagen, interval.datapush
 *
 *  NC - March 2018
 */
//-------------------------------------------------------------------------------------------------

#include "legato.h"
#include "interfaces.h"

#include "position.h"   //Use position helper lib to push the current location to AirVantage
#include "gpio_iot.h"   //Use gpio_iot helper lib to manage the door LED, door switch and fan motor, connected to IoT card's GPIO pin (24, 25, 26)

//Config tree path of settings for persistency
#define CONFIG_DATAPUSH_INTERVAL			"/fridgeTruck/DataPushInterval"
#define CONFIG_DATAGEN_INTERVAL				"/fridgeTruck/DataGenInterval"

#define CONFIG_AIR_TEMPERATURE				"/fridgeTruck/OutsideTemperature"
#define CONFIG_TARGET_TEMPERATURE			"/fridgeTruck/TargetTemperature"

//GPIO pins to be used on the IoT card
#define GPIO_PIN_DOOR_SWITCH				1
#define GPIO_PIN_DOOR_LED					2
#define GPIO_PIN_FAN_MOTOR					3

//AV System Data Variables
#define VARIABLE_FAN_STATE      			"truck.var.fan.isOn"                //boolean : fan is on or off
#define VARIABLE_FAN_DURATION   			"truck.var.fan.duration"            //int : how long it's been functioning (minute)
#define VARIABLE_TEMP_CURRENT   			"truck.var.temp.current"            //float : current temperature
#define VARIABLE_DOOR_STATE     			"truck.var.door.isOpen"             //boolean : door is opened or closed

static bool 	                 			_fanIsOn = true;
static int 	                    			_fanDuration = 0;
static double                   			_temperature = 4.2;                 //current temp
static bool 	                 			_doorIsOpen = false;


//AV System Data Settings
#define SETTING_TEMP_TARGET     			"truck.set.temp.target"             //float : target regulated temperature
#define SETTING_TEMP_AIR        			"truck.set.temp.outside"            //int : outside air temperature
#define SETTING_DATAGEN_INTERVAL 			"truck.set.interval.datagen"
#define SETTING_DATAPUSH_INTERVAL			"truck.set.interval.datapush"
#define SETTING_MANGOH_TYPE					"truck.set.mangohType"

#define FIELDNAME_DATAGEN					"datagen"
#define FIELDNAME_DATAPUSH					"datapush"
#define FIELDNAME_AIR_TEMP					"outside"
#define FIELDNAME_AC_TARGET_TEMP			"target"
#define FIELDNAME_MANGOH_TYPE				"mangohType"

static double                   			_temperatureTarget = 2.2;
static int                      			_temperatureOutside = 27;
static int									_dataGenInterval = 5;				//5 seconds
static int									_dataPushInterval = 20;				//30 seconds
static gpio_iot_mangohType_t                _mangohBoardType;                   //type of mangOH board (Red, Green)

//AV Commands
#define COMMAND_FAN_START       			"truck.cmd.startFan"                //Start fan
#define COMMAND_FAN_STOP        			"truck.cmd.stopFan"                 //Stop fan
#define FIELDNAME_START_FAN					"startFan"
#define FIELDNAME_STOP_FAN					"stopFan"
#define COMMAND_OPEN_DOOR       			"truck.cmd.openDoor"                //Open door
#define COMMAND_CLOSE_DOOR        			"truck.cmd.closeDoor"                 //Close door
#define FIELDNAME_OPEN_DOOR					"openDoor"
#define FIELDNAME_CLOSE_DOOR				"closeDoor"

//Default behavior
#define DEFAULT_START_TEMP 					5.2         //default starting point of the current temperature
#define TIMESERIE_MAX_RECORD				6           //number of record to accumulated before pushing to AV as timeserie
#define TEMPERATURE_INC_STEP				0.4         //temperature increment step for simulation
#define FAN_DURATION_INC_STEP   			5           //Fan duration increment step for the simulation


//Other
static le_timer_Ref_t						_dataGenTimerRef = NULL;              //reference to data generation/simulation timer
static le_timer_Ref_t						_dataPushTimerRef = NULL;             //reference to push data timer
static le_avdata_RecordRef_t 				_recordRef = NULL;                    //reference to the timeserie data
static int 									_recordCount = 0;                     //timeserie record counter

static le_avdata_RequestSessionObjRef_t		_truck_requestSessionRef = NULL;      //reference to AV session request


// Save current settings to config tree
void SaveConfig()
{
	le_cfg_QuickSetInt(CONFIG_DATAGEN_INTERVAL, _dataGenInterval);

	le_cfg_QuickSetInt(CONFIG_DATAPUSH_INTERVAL, _dataPushInterval);

	le_cfg_QuickSetInt(CONFIG_AIR_TEMPERATURE, _temperatureOutside);

	le_cfg_QuickSetFloat(CONFIG_TARGET_TEMPERATURE, _temperatureTarget);
}


// Load parameters from config tree
void LoadConfig()
{
	int 	cfgValue;
	bool	save = false;

	cfgValue = le_cfg_QuickGetInt(CONFIG_DATAGEN_INTERVAL, -1);
	if (cfgValue < 0)
	{
		save = true;
	}
	else
	{
		_dataGenInterval = cfgValue;
	}
	LE_INFO("Data Gen Interval is %d seconds...", _dataGenInterval);

	cfgValue = le_cfg_QuickGetInt(CONFIG_DATAPUSH_INTERVAL, -1);
	if (cfgValue < 0)
	{
		save = true;
	}
	else
	{
		_dataPushInterval = cfgValue;
	}	
	LE_INFO("Data Pushing Interval is %d seconds...", _dataPushInterval);

	
	cfgValue = le_cfg_QuickGetInt(CONFIG_AIR_TEMPERATURE, -1);
	if (cfgValue < 0)
	{
		save = true;
	}
	else
	{
		_temperatureOutside = cfgValue;
		_temperature = DEFAULT_START_TEMP;
	}	
	LE_INFO("Air Temperature is %d degrees...", _temperatureOutside);


	float fValue = le_cfg_QuickGetFloat(CONFIG_TARGET_TEMPERATURE, 0.05);
	if (fValue == 0.05)
	{
		save = true;
	}
	else
	{
		_temperatureTarget = fValue;
	}
	LE_INFO("Target Temperature is %f degrees...", _temperatureTarget);

	if (save)
	{
		//missing keys in config tree, let's save default values to config tree
		SaveConfig();
	}
}

//callback to handle the data push status
static void PushDataCallbackHandler
(
    le_avdata_PushStatus_t status,
    void* contextPtr
)
{
    LE_INFO("PushDataCallbackHandler : %d", status);

    if (LE_AVDATA_PUSH_FAILED == status)
    {
    	LE_INFO("Failed to Push Data... check connection !");
    }
    else
    {
    	LE_INFO("Push Data OK & ACKed");
    }

}

//push the Fan status and Door status, trigger by the dataPush timer
void pushData(le_timer_Ref_t  timerRef)
{
	LE_INFO("--- Pushing data to AV...");

	le_avdata_SetBool(VARIABLE_FAN_STATE, _fanIsOn);
	if (LE_FAULT == le_avdata_Push(VARIABLE_FAN_STATE, PushDataCallbackHandler, NULL))
	{
		LE_INFO("Failed to push Fan State");
	}

	le_avdata_SetBool(VARIABLE_DOOR_STATE, _doorIsOpen);
	if (LE_FAULT == le_avdata_Push(VARIABLE_DOOR_STATE, PushDataCallbackHandler, NULL))
	{
		LE_INFO("Failed to push Door State");
	}
}

//function to switch the fan on/off, and turning the fan motor on/off. Can also push the new fan status to AV
void SwitchFan(bool bturnOn, bool pushData)
{
	_fanIsOn = bturnOn;

	if (pushData)
	{
		le_avdata_SetBool(VARIABLE_FAN_STATE, _fanIsOn);
		if (LE_FAULT == le_avdata_Push(VARIABLE_FAN_STATE, PushDataCallbackHandler, NULL))
		{
			LE_INFO("Failed to push Fan State");
		}
	}

	gpio_iot_SetOutput(GPIO_PIN_FAN_MOTOR, _fanIsOn);

	if (!_fanIsOn)
	{
		_fanDuration = 0;
	}
}

//function to open/close the door, and turning the door led on/off. Can also push the new door status to AV
void SwitchDoor(bool bOpen, bool pushData)
{
	_doorIsOpen = bOpen;

	if (pushData)
	{
		le_avdata_SetBool(VARIABLE_DOOR_STATE, _doorIsOpen);
		if (LE_FAULT == le_avdata_Push(VARIABLE_DOOR_STATE, PushDataCallbackHandler, NULL))
		{
			LE_INFO("Failed to push Door State");
		}
	}

	gpio_iot_SetOutput(GPIO_PIN_DOOR_LED, _doorIsOpen);	
}

// Callback function to handle Write Request from AV
void OnWriteSetting
(
	const char* path,
    le_avdata_AccessType_t accessType,
    le_avdata_ArgumentListRef_t argumentList,
    void* contextPtr
)
{
	LE_INFO("*** OnWriteSetting *** : %s", path);

	if (strstr(path, FIELDNAME_DATAGEN) != NULL)
	{
		int  newSetting = 0;

		LE_INFO("Setting Change: DataGenInterval was %d seconds", _dataGenInterval);
		le_avdata_GetInt(SETTING_DATAGEN_INTERVAL, &newSetting);               //Get the new setting from AirVantage

		if (newSetting != _dataGenInterval)
		{
			_dataGenInterval = newSetting;
			LE_INFO("Setting Change: DataGenInterval is now %d seconds", _dataGenInterval);
			SaveConfig();
		
			le_clk_Time_t      interval = { _dataGenInterval, 0 };                      //update temperature every 5 seconds
			le_timer_Stop(_dataGenTimerRef);
			le_timer_SetInterval(_dataGenTimerRef, interval);
			le_timer_Start(_dataGenTimerRef);
		}
		else
		{
			LE_INFO("Setting Change: DataGenInterval is unchanged: %d seconds", _dataGenInterval);
		}
	}
	else if (strstr(path, FIELDNAME_DATAPUSH) != NULL)
	{
		int  newSetting = 0;

		LE_INFO("Setting Change: DataPushInterval was %d seconds", _dataPushInterval);
		le_avdata_GetInt(SETTING_DATAPUSH_INTERVAL, &newSetting);               //Get the new setting from AirVantage

		if (newSetting != _dataPushInterval)
		{
			_dataPushInterval = newSetting;
			LE_INFO("Setting Change: DataPushInterval is now %d seconds", _dataPushInterval);
			SaveConfig();

			le_clk_Time_t      interval = { _dataPushInterval, 0 };                      //update temperature every 5 seconds
			le_timer_Stop(_dataPushTimerRef);
			le_timer_SetInterval(_dataPushTimerRef, interval);
			le_timer_Start(_dataPushTimerRef);
		}
		else
		{
			LE_INFO("Setting Change: DataPushInterval is unchanged: %d seconds", _dataPushInterval);
		}
	}
	else if (strstr(path, FIELDNAME_AC_TARGET_TEMP) != NULL)
	{
		LE_INFO("Setting Change: AC-Temperature was %f C°", _temperatureTarget);
		le_avdata_GetFloat(SETTING_TEMP_TARGET, &_temperatureTarget);               //Get the new setting from AirVantage

		LE_INFO("Setting Change: AC-Temperature is now %f C°", _temperatureTarget);
        SaveConfig();
	}
	else if (strstr(path, FIELDNAME_AIR_TEMP) != NULL)
	{
		LE_INFO("Setting Change: Air Temperature was %d C°", _temperatureOutside);
		le_avdata_GetInt(SETTING_TEMP_AIR, &_temperatureOutside);               //Get the new setting from AirVantage

		LE_INFO("Setting Change: Air Temperature is now %d C°", _temperatureOutside);
		SaveConfig();
	}
	else if (strstr(path, FIELDNAME_MANGOH_TYPE) != NULL)
	{
		LE_INFO("Setting Change: mangOH board type was %d (0 = Red, 1 = Green, 2=Yellow)", _mangohBoardType);
		int	value;
		le_avdata_GetInt(SETTING_MANGOH_TYPE, &value);               //Get the new setting from AirVantage

		_mangohBoardType = value;
		gpio_iot_SetMangohType(_mangohBoardType);

		LE_INFO("Setting Change: mangOH board type is now %d (0 = Red, 1 = Green, 2=Yellow)", _mangohBoardType);
		SaveConfig();
	}
	
}

//Callback function to handle Command Execution Request from AV
void OnCommand
(
	const char* path,
    le_avdata_AccessType_t accessType,
    le_avdata_ArgumentListRef_t argumentList,
    void* contextPtr
)
{
	LE_INFO("*** OnCommand *** : %s", path);

	if (strstr(path, FIELDNAME_START_FAN) != NULL)
	{
		LE_INFO("Execute Command Request: StartFan");
		SwitchFan(true, true);
	}
	else if (strstr(path, FIELDNAME_STOP_FAN) != NULL)
	{
		LE_INFO("Execute Command Request: StopFan");
		SwitchFan(false, true);
	}
	else if (strstr(path, FIELDNAME_OPEN_DOOR) != NULL)
	{
		LE_INFO("Execute Command Request: OpenDoor");
		SwitchDoor(true, true);
	}
	else if (strstr(path, FIELDNAME_CLOSE_DOOR) != NULL)
	{
		LE_INFO("Execute Command Request: CloseDoor");
		SwitchDoor(false, true);
	}

	le_avdata_ReplyExecResult(argumentList, LE_OK);
}

//Callback function to handle the timeserie push status
void PushRecordCallbackHandler
(
    le_avdata_PushStatus_t status,
    void* contextPtr
)
{
    if (status == LE_AVDATA_PUSH_SUCCESS)
 	{
 		LE_INFO("Push Timeserie OK");
 	}
 	else
 	{
 		LE_INFO("Failed to push Timeserie");
 	}
}


//Function to accumulate the current temperature and fan duration in a timeserie record
//create a new record if doesn't exist, if the number of record reach TIMESERIE_MAX_RECORD then push the serie to AV
void Accumulate()
{
	struct timeval 	tv;
    gettimeofday(&tv, NULL);
	uint64_t	utcMilliSec = (uint64_t)(tv.tv_sec) * 1000 + (uint64_t)(tv.tv_usec) / 1000;

	bool		pushNow = false;

	if (_recordRef == NULL)
	{
		le_pos_FixState_t 	fixStatePtr;
		position_PushLocation(&fixStatePtr);

		LE_INFO("Creating new Record");
		_recordRef = le_avdata_CreateRecord();
		_recordCount = 0;
	}

	le_result_t result = le_avdata_RecordFloat(_recordRef, VARIABLE_TEMP_CURRENT, _temperature, utcMilliSec);
	result = le_avdata_RecordInt(_recordRef, VARIABLE_FAN_DURATION, _fanDuration, utcMilliSec);

	if (LE_OK == result)
	{
		_recordCount++;

		if (_recordCount >= TIMESERIE_MAX_RECORD)
		{
			pushNow = true;
		}
	}
	else if (result == LE_NO_MEMORY || result == LE_OVERFLOW)
    {
        LE_INFO("Buffer Overflow or Full, Now Pushing timeseries");
        
        pushNow = true;       
	}
	else
	{
		LE_INFO("Unknown Accumulation outcome");
	}

	if (pushNow)
	{
		result = le_avdata_PushRecord(_recordRef, PushRecordCallbackHandler, NULL);

        if (LE_OK != result)
        {
        	LE_INFO("Failed pushing timeseries");
        }
        //else if (LE_OK == result)		//discard anyway even push failed
        {
        	le_avdata_DeleteRecord(_recordRef);
        	_recordRef = NULL;
        } 
	}
}

//Converge a temperature to a given target temp
void converge(double target, double step, double* value)
{
    if (*value < target)
    {
        *value += step;
    }
    else
    {
        *value -= step;
    }
}

//Simulate the envisaged scenario (refer to header of this file)
void emulate(le_timer_Ref_t  timerRef)
{
	if (_fanIsOn && !_doorIsOpen)
    {
        //if door is closed and fan is on then converge to target temp
        converge(_temperatureTarget, TEMPERATURE_INC_STEP, &_temperature);
        
        LE_INFO("Converging to Target temp (%f °C) - Current temperature = %f", _temperatureTarget, _temperature);

        if (_temperature <= _temperatureTarget)
        {
            LE_INFO("Reach Target temp, turning off Fan");
            SwitchFan(false, true);   //reach target temp, turn off Fan
        }
    }
    else
    {
        //otherwise, converge to outside temp
        converge((double)_temperatureOutside, TEMPERATURE_INC_STEP, &_temperature);

        LE_INFO("Converging to Outside temp (%d °C) - Current temperature = %f", _temperatureOutside, _temperature);
    }

    if (_fanIsOn)
    {
        _fanDuration += FAN_DURATION_INC_STEP;
    }

    le_avdata_SetFloat(VARIABLE_TEMP_CURRENT, _temperature);
    le_avdata_SetInt(VARIABLE_FAN_DURATION, _fanDuration);

    Accumulate();
}

//callback function to handle the door push button transition : just toggle the door status
static void OnDoorSwitchChangeCallback(bool state, void *ctx)
{
    LE_INFO("Door State change %s", state?"TRUE":"FALSE");

    bool output = gpio_iot_Read(GPIO_PIN_DOOR_LED);

    SwitchDoor(!output, true);
}

//Setting up the push button door switch assigned to GPIO_1
void SetupDoorSwitchGpio()		//Use GPIO_1 as an Input, a push button to open/close door
{
	gpio_iot_SetInput(GPIO_PIN_DOOR_SWITCH, true);
	gpio_iot_EnablePullUp(GPIO_PIN_DOOR_SWITCH);
	//if the button is pushed, then call OnGpio1Change callback function
	gpio_iot_AddChangeEventHandler(GPIO_PIN_DOOR_SWITCH, GPIO_IOT_EDGE_RISING, OnDoorSwitchChangeCallback, NULL, 100);
}

//Setting up the fan motor assigned to GPIO_3
void SetupFanGpio()		//Use GPIO_3 to drive the Fan (motor)
{
	gpio_iot_SetPushPullOutput(GPIO_PIN_FAN_MOTOR, true, true);
}

//Setting up the Door Led assigned to GPIO_2
void SetupDoorLedGpio()		//Use GPIO_2 to drive a LED, as an indication of door status (open/close)
{
	gpio_iot_SetPushPullOutput(GPIO_PIN_DOOR_LED, true, true);
}

//callback function to handle program exit tasks
static void sig_appTermination_cbh(int sigNum)
{
	position_Stop();

	if (_truck_requestSessionRef)
	{
        //Release the session with AirVantage
		le_avdata_ReleaseSession(_truck_requestSessionRef);
	}
}

//main start
COMPONENT_INIT
{
	LE_INFO("Starting Refrigerated Truck App");

	le_sig_Block(SIGTERM);
	le_sig_SetEventHandler(SIGTERM, sig_appTermination_cbh);

    //Open a session with AirVantage
	_truck_requestSessionRef = le_avdata_RequestSession();

	//retrieve default settings from config tree
    LoadConfig();

    //data path is not prefixed by application name
	le_avdata_SetNamespace(LE_AVDATA_NAMESPACE_GLOBAL);

	//Start positioning service
	position_Start();

	//Setup GPIOs
	gpio_iot_Init();
	_mangohBoardType = gpio_iot_GetMangohType();	//Get the type of mangOH board being used, saved in config tree handled by the gpio_iot helper lib
	SetupFanGpio();
	SetupDoorLedGpio();
	SetupDoorSwitchGpio();

	//Create Variables
	le_avdata_CreateResource(VARIABLE_FAN_STATE, LE_AVDATA_ACCESS_VARIABLE);
    SwitchFan(_fanIsOn, false);

    le_avdata_CreateResource(VARIABLE_FAN_DURATION, LE_AVDATA_ACCESS_VARIABLE);
    le_avdata_SetInt(VARIABLE_FAN_DURATION, _fanDuration);

    le_avdata_CreateResource(VARIABLE_TEMP_CURRENT, LE_AVDATA_ACCESS_VARIABLE);
    le_avdata_SetFloat(VARIABLE_TEMP_CURRENT, _temperature);

    le_avdata_CreateResource(VARIABLE_DOOR_STATE, LE_AVDATA_ACCESS_VARIABLE);
    SwitchDoor(_doorIsOpen, false);


    //Create Settings
    le_avdata_CreateResource(SETTING_DATAGEN_INTERVAL, LE_AVDATA_ACCESS_SETTING);
    le_avdata_SetInt(SETTING_DATAGEN_INTERVAL, _dataGenInterval);
    le_avdata_AddResourceEventHandler(SETTING_DATAGEN_INTERVAL, OnWriteSetting, NULL);

    le_avdata_CreateResource(SETTING_DATAPUSH_INTERVAL, LE_AVDATA_ACCESS_SETTING);
    le_avdata_SetInt(SETTING_DATAPUSH_INTERVAL, _dataPushInterval);
    le_avdata_AddResourceEventHandler(SETTING_DATAPUSH_INTERVAL, OnWriteSetting, NULL);

    le_avdata_CreateResource(SETTING_TEMP_TARGET, LE_AVDATA_ACCESS_SETTING);
    le_avdata_SetFloat(SETTING_TEMP_TARGET, _temperatureTarget);
    le_avdata_AddResourceEventHandler(SETTING_TEMP_TARGET, OnWriteSetting, NULL);

    le_avdata_CreateResource(SETTING_TEMP_AIR, LE_AVDATA_ACCESS_SETTING);
    le_avdata_SetInt(SETTING_TEMP_AIR, _temperatureOutside);
    le_avdata_AddResourceEventHandler(SETTING_TEMP_AIR, OnWriteSetting, NULL);

    le_avdata_CreateResource(SETTING_MANGOH_TYPE, LE_AVDATA_ACCESS_SETTING);
    le_avdata_SetInt(SETTING_MANGOH_TYPE, _mangohBoardType);
    le_avdata_AddResourceEventHandler(SETTING_MANGOH_TYPE, OnWriteSetting, NULL);
    


    //Create Command
    le_avdata_CreateResource(COMMAND_FAN_START, LE_AVDATA_ACCESS_COMMAND);
    le_avdata_AddResourceEventHandler(COMMAND_FAN_START, OnCommand, NULL);

    le_avdata_CreateResource(COMMAND_FAN_STOP, LE_AVDATA_ACCESS_COMMAND);
    le_avdata_AddResourceEventHandler(COMMAND_FAN_STOP, OnCommand, NULL);

    le_avdata_CreateResource(COMMAND_OPEN_DOOR, LE_AVDATA_ACCESS_COMMAND);
    le_avdata_AddResourceEventHandler(COMMAND_OPEN_DOOR, OnCommand, NULL);

    le_avdata_CreateResource(COMMAND_CLOSE_DOOR, LE_AVDATA_ACCESS_COMMAND);
    le_avdata_AddResourceEventHandler(COMMAND_CLOSE_DOOR, OnCommand, NULL);

    
  
	//Set timer to update/simulate data on a regularyly basis
	_dataGenTimerRef = le_timer_Create("dataGenTimer");     //create timer

	le_clk_Time_t      interval = { _dataGenInterval, 0 };                      //update temperature every 5 seconds
	le_timer_SetInterval(_dataGenTimerRef, interval);
	le_timer_SetRepeat(_dataGenTimerRef, 0);                   //set repeat to always

	//set callback function to handle timer expiration event
	le_timer_SetHandler(_dataGenTimerRef, emulate);

	emulate(_dataGenTimerRef);

	//start timer
	le_timer_Start(_dataGenTimerRef);



	//Set timer to create and push timeSerie
	_dataPushTimerRef = le_timer_Create("dataPushTimer");     //create timer

	le_clk_Time_t      interval2 = { _dataPushInterval, 0 };                      //update temperature every 5 seconds
	le_timer_SetInterval(_dataPushTimerRef, interval2);
	le_timer_SetRepeat(_dataPushTimerRef, 0);                   //set repeat to always

	//set callback function to handle timer expiration event
	le_timer_SetHandler(_dataPushTimerRef, pushData);

	pushData(_dataPushTimerRef);

	//start timer
	le_timer_Start(_dataPushTimerRef);

}
