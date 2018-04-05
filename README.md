Legato-AirVantage : Asset Data feature
======================================

This sample Legato application demonstrates data exchange feature between an embedded Legato application and AirVantage server, over the LWM2M protocol.

This application has been tested on Sierra Wireless AirPrime WP76xx module running Legato 18.02 and hosted on MangOH board. 

Asset Data API introduction
---------------------------

* __Variables__, __Settings__ and __Commands__
	- Refer to [Asset Data](http://legato.io/legato-docs/latest/avData.html) for how to create these 3 types of data on the device side. This page also describes how to handle server requests sent from AirVantage : Read request on Variables, Write request on Settings and Execute request on Commands.
	- Refer to [Asset Data Models](http://legato.io/legato-docs/latest/avData.html#avData_DataModel) for how to define them on AirVantage (server side). Asset Data Models is only required if you want to send server (Read/Write/Execute) requests to device using AirVantage Portal GUI, or if you need to use AirVantage portal's Dashboard to display device data
	- Use le_avdata_SetInt(), le_avdata_SetString(), le_avdata_SetFloat() or le_avdata_SetBool() to assign a value to a data 
	- Use le_avdata_Push() function to sent the data/value pair to AirVantage 
* __Timeseries__ - Instead of sending data frequently to AirVantage (using le_avdata_Push), you can accumulate data along with timestamps (can be done offline) into Record. Then send the collected data at once later (i.e. when device is online) :
	- Create a Record : le_avdata_CreateRecord()
	- Accumulate data into the record: le_avdata_RecordInt(), le_avdata_RecordString(), le_avdata_RecordFloat() or le_avdata_RecordBool()
	- Push the record to AirVantage : le_avdata_PushRecord()
	- Delete the record (upon pushing) : le_avdata_DeleteRecord()
	- Refer to [Timeseries](http://legato.io/legato-docs/latest/avData.html#avData_TimeSeries) for further details




 
Sample Application
------------------
This application is simulating a refrigerated truck (e.g. transporting frozen/fresh foods, blood).
The implemented scenario is described in the header of fridgeTruck.c.
This application is making use of :
* [le_avdata](http://legato.io/legato-docs/latest/le__avdata__interface_8h.html) (Legato asset data API) to send single data point and Timeseries to AirVantage. The app can handle AirVantage requests (change settings, execute commands)
* [le_cfg](http://legato.io/legato-docs/latest/le__cfg__interface_8h.html) (Legato config tree API) to persist some settings in the config tree
* [position helper library](https://github.com/nhonchu/Legato-Positioning-sample) (wrapping Legato's positioning service: le_pos and le_posCtrl) to push the current geolocation of the device to AirVantage
* [gpio helper library](https://github.com/nhonchu/Legato-GPIO-sample) (wrapping Legato's le_gpio service) to provide visual feedback on the AC Fan (motor) and the truck door status (LED). A switch (push button) is also implemented to open/close truck door. The motor, LED and push button are wired to a [IoT expansion card](https://mangoh.io/iot-cards) and plugged into IoT slot0 of a [mangOH board](https://mangoh.io) (Red or Green). Transistor should be used to drive motor and LED.

Build
-----
To build executable for WP76xx:
~~~
make wp76xx
~~~

Install the app on target (MangOH/WP76XX)
-----------------------------------------
~~~
app install fridgeTruck.wp76xx.update <IP address of target>
~~~
The app will start automatically

Create an update package & Generate package for upload to AirVantage
--------------------------------------------------------------------
Do the following only Once:
~~~
av-pack -u fridgetTruck.wp76xx.update -b _build_fridgeTruck/wp76xx/ -t <TypeName>
	where <TypeName> shall be globally unique (e.g. WP76_LE1802_YourNAME_FridgeTruck). AirVantage uses it to uniquely identify multiple versions of this application
~~~

The above commands will create a zip file containing an *update package* and a *manifest xml file* (manifest.app).

Adding Asset Data Models to update package
------------------------------------------
The manifest.app file generated in the previous step will be used by AirVantage to gain knowledge on the application. However manifest.app does not provide information of the asset data models supported by our application. Insert the following model into manisfest.app (right after the existing *asset* element) :

```
	<asset default-label="Truck" id="truck">

		<node default-label="Variables" path="var">
			<variable default-label="Fan Status" path="fan.isOn" type="boolean"/>
			<variable default-label="Door Status" path="door.isOpen" type="boolean"/>
			<variable default-label="Temperature" path="temp.current" type="double"/>
			<variable default-label="Fan Duration" path="fan.duration" type="int"/>
		</node>

		<node default-label="Settings" path="set">
			<setting default-label="Air Temperature" path="temp.outside" type="int"/>
			<setting default-label="Target Temperature" path="temp.target" type="double"/>
			<setting default-label="Data Gen interval" path="interval.datagen" type="int"/>
			<setting default-label="Data Push interval" path="interval.datapush" type="int"/>
			<setting default-label="mangOH 0-Red 1-Green" path="mangohType" type="int"/>
		</node>

		<node default-label="Commands" path="cmd">
			<command default-label="Start Fan" path="startFan"/>
			<command default-label="Stop Fan" path="stopFan"/>
			<command default-label="Open Door" path="openDoor"/>
			<command default-label="Close Door" path="closeDoor"/>
		</node>

	</asset>
```

Replace the modified manisfest.app back to the zip file. [Release](https://doc.airvantage.net/avc/reference/develop/howtos/releaseApplication/) this package to AirVantage.


Testing
-------

Compile and install the app
	make wp76xx
	app install fridgeTruck.wp76xx.update 192.168.2.2

![](serverCommand.gif "Send Commands from AirVantage!")
