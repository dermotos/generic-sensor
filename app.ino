
#include "DynamicCommandParser.h"
#include "OneButton.h"
#include "captouch.h"

#define CENTRAL_CONTROL_IP 10,0,0,40
#define CENTRAL_CONTROL_PORT 9998

SYSTEM_MODE(SEMI_AUTOMATIC);


int MOTION_TIMEOUT = 30 * 1000; // Defaults to 30 seconds. Programmable via central control.
//int LED_TEST = D7;
int BUTTON_NORTH = D4;
int BUTTON_EAST = D7;
int BUTTON_SOUTH = D6;
int BUTTON_WEST = D5;
int LED_NORTH = A4;
int LED_EAST = A7;
int LED_SOUTH = A6;
int LED_WEST = A5;

int POT_PIN = A0;

int MOTION_PIN_ONE = A1;
int MOTION_PIN_TWO = A2;
int MOTION_PIN_THREE = A3;


//Disconnect occurs in the sensor when motion is detected (pin three), otherwise it's connected.
// Therefore, the other wire going to sensor is connected to ground. When there is no motion, this will be closed (NC = normally closed).
// When motion IS detected, it is disconnected and the pull up resistor pulls it up to HIGH. Hence, it uses an internal pull_up resistor.

// ** Capacitive sensor **
// Define which pins are connected with a 1-10M resistor.
// The first pin will be connected to the touch sensor
// and *must* be D0, D1, D2, D3, D4 A0, A1, A3, A4, A5, A6, A7
// see: http://docs.spark.io/firmware/#interrupts-attachinterrupt
CapTouch Touch(D3, D2);

int EXTRA_GND_0 = D0;
int EXTRA_GND_1 = D1;

//Potentiometer should only report when it has changed by a value greater than 10
bool motionDebugging = false;
int lastPotReadingValue = 0;
unsigned long lastPotReadingTime = 0;
unsigned long potReadingInterval = 100; //Check the pot reading a max of 5 times per second.
int potReadingStep = 3; //How much of a change is required in the pot value to send a new changes to central control.
unsigned long checkInInterval = 1000 * 30; //Checkin with central control every xx seconds
unsigned long nextCheckIn = 0;
int connectionRetries = 0; //Keeps track of the number of reconnection tries. If it exceeds connectionRetryLimit, the system is reset.
const int connectionRetryLimit = 5;

unsigned long centralHeartbeatInterval = 60 * 1000; //Expect a heartbeat from central control every 60 seconds (usually less).
unsigned long centralHeartbeatLastReceived = 0; //Keep track of the last time we received a heartbeat.


bool humanPresent = false;
unsigned int nextMotionTimeout = 0;

int MAX_BRIGHTNESS = 99; //A semi-constant. This can be changed by central control via a set-brightness command.
int LED_OFF = 0;



// Button results
int function = 0;
// Device contains 4 buttons in a diamond/compass point pattern.
OneButton northButton(BUTTON_NORTH,INPUT_PULLUP);
OneButton southButton(BUTTON_SOUTH,INPUT_PULLUP);
OneButton eastButton(BUTTON_EAST,INPUT_PULLUP);
OneButton westButton(BUTTON_WEST,INPUT_PULLUP);

String deviceIdentifier;

// Network connectivity
TCPClient client;
byte server[] = { CENTRAL_CONTROL_IP };
int port = CENTRAL_CONTROL_PORT;
String incomingBuffer;

DynamicCommandParser commandParser('^','$',',');

void resetDevice(char **values, int valueCount){
  System.reset();
}

void connectToCloud(char **values, int valueCount){
  Spark.connect();
}


void setup() {
    String thisDevice = Spark.deviceID();
    String bedsideCoreID =        String("51ff6c065067545728090187");
    String bedroomDoorCoreID =    String("48ff6d065067555010282287");
    String couchCoreID =          String("53ff6a065067544811321287");
    String deskCoreID =           String("51ff6d065067545744360687");
    String kitchenCoreID =        String("50ff6b065067545634060287");
    String tvCoreID =             String("53ff6a065067544838230187");
    String bathroomID =           String("48ff6f065067555033221387");

   // pinMode(LED_TEST,OUTPUT);
    //Bedside: 51ff6c065067545728090187
    if(thisDevice == bedsideCoreID){
      deviceIdentifier = "bedside";
    }
    else if(thisDevice == bedroomDoorCoreID){
      deviceIdentifier = "bedroom-door";
    }
    else if(thisDevice == couchCoreID){
      deviceIdentifier = "couch";
    }
    else if(thisDevice == deskCoreID){
      deviceIdentifier = "desk";
    }
    else if(thisDevice == kitchenCoreID){
      deviceIdentifier = "kitchen";
    }
    else if(thisDevice == bathroomID){
      deviceIdentifier = "bathroom";
      MOTION_TIMEOUT = (1000 * 60 * 3); // Longer default timeout in bathroom of 3 minutes
    }
    else if(thisDevice == tvCoreID){
      deviceIdentifier = "tv";
      checkInInterval = 1000 * 4;
      /* Check in every 4 seconds.
       * The TV device is connected to the TV's USB port. It's sole purpose is to continiously check in when powered up,
       * hence why the frequency is much higher.
       */

    }

    pinMode(BUTTON_NORTH, INPUT_PULLUP);

    pinMode(LED_NORTH,OUTPUT);
    pinMode(LED_SOUTH,OUTPUT);
    pinMode(LED_EAST,OUTPUT);
    pinMode(LED_WEST,OUTPUT);

    pinMode(POT_PIN,INPUT);
    pinMode(MOTION_PIN_ONE,INPUT_PULLDOWN);
    pinMode(MOTION_PIN_TWO,INPUT_PULLDOWN);
    pinMode(MOTION_PIN_THREE,INPUT_PULLUP); //This uses a different sensor type, hence the pull_up.

    pinMode(EXTRA_GND_0,OUTPUT);
    digitalWrite(EXTRA_GND_0,LOW);
    pinMode(EXTRA_GND_1,OUTPUT);
    digitalWrite(EXTRA_GND_1,LOW);

    // Connect the event handlers for the buttons
    northButton.attachClick(northClick);
    northButton.attachDoubleClick(northDoubleClick);
    northButton.attachPress(northLongClick);

    southButton.attachClick(southClick);
    southButton.attachDoubleClick(southDoubleClick);
    southButton.attachPress(southLongClick);

    eastButton.attachClick(eastClick);
    eastButton.attachDoubleClick(eastDoubleClick);
    eastButton.attachPress(eastLongClick);

    westButton.attachClick(westClick);
    westButton.attachDoubleClick(westDoubleClick);
    westButton.attachPress(westLongClick);

    commandParser.addParser("set-led-state", setLedState);
    commandParser.addParser("set-led-brightness", setLedBrightness);
    commandParser.addParser("heartbeat",centralHeartbeat);
    commandParser.addParser("reset",resetDevice);
    commandParser.addParser("connect-to-cloud",connectToCloud);
    commandParser.addParser("cancel-motion",cancelMotion);
    commandParser.addParser("motion-timeout",setMotionTimeout);
    commandParser.addParser("motion-debugging",setMotionDebugging);


    if(WiFi.ready() == false){
        WiFi.connect();
    }

    while(WiFi.ready() == false){
        delay(50);
    }
    Serial.begin(9600);

    delay(500);
    checkIn();
    startupLedSequence();

    lastPotReadingTime = millis() + 5000;


    Touch.setup();
    // Prints out the device ID over Serial
    Serial.print("Im the ");
    Serial.print(deviceIdentifier);
    Serial.print(" device and my ID is ");
    Serial.println(thisDevice);

    RGB.control(true);
    RGB.brightness(0);


}


void cancelMotion(char **values, int valueCount){
  humanPresent = false;
  nextMotionTimeout = 0;
}




void updateMotion(int motionSensor){
    bool motionStateChanged = false;
    bool currentMotionState = false;

    if(motionSensor == 1){
      currentMotionState = digitalRead(MOTION_PIN_ONE);
    }
    else if(motionSensor == 3){
      currentMotionState = digitalRead(digitalRead(MOTION_PIN_THREE));
    }
    //currentMotionState = (digitalRead(MOTION_PIN_ONE) || digitalRead(MOTION_PIN_TWO) || digitalRead(MOTION_PIN_THREE));

    /* DEBUGGING MOTION SENSORS */
    /* Lights on buttons are used during debugging to signal the state of motion sensors */
    if(motionDebugging){
      digitalWrite(LED_NORTH,digitalRead(MOTION_PIN_THREE));
      digitalWrite(LED_SOUTH,digitalRead(MOTION_PIN_TWO) || digitalRead(MOTION_PIN_ONE));
    }
    Serial.print("Motion Debugging:");
    Serial.println(digitalRead((currentMotionState)));
    /* ------------------------ */

    if(humanPresent == true && currentMotionState == true){
        //Motion is considered active, and still is, set the motion timeout again:
        nextMotionTimeout = millis() + MOTION_TIMEOUT;
        Serial.println("Motion still active. Re-setting timeout.");
    }
    else if(humanPresent == true && currentMotionState == false){
        Serial.println("No recent motion, but still considered active");
        //Motion is considered active, but no recent motion has been detected. This clause here for completeness. The timeout continues to approach...
    }
    else if(humanPresent == false && currentMotionState == true){
        Serial.println("New motion detected");
        //There was no motion, but there is now.
        motionStateChanged = true;
        humanPresent = true;
        nextMotionTimeout = millis() + MOTION_TIMEOUT;
    }
    else if(humanPresent == false && currentMotionState == false){
        //No movement
        Serial.println("No movement");
    }

    //Check has the motion timeout elapsed:
    if(nextMotionTimeout != 0 && nextMotionTimeout < millis())
    {
        Serial.println("Motion state has changed");
        motionStateChanged = true;
        nextMotionTimeout = 0;
        humanPresent = false; //A timeout has occurred.
    }

    if(motionStateChanged){
        if(humanPresent){
            sendMessage("motion-started");
            Serial.println("Motion started");
        }
        else{
            sendMessage("motion-stopped");
            Serial.println("Motion stopped");
        }
    }

}












void centralHeartbeat(char **values, int valueCount){
  // A heartbeat command from central was received. If one hasn't been received within 60 seconds, the device will attempt to re-establish
  // TCP connection on next outbound communication attempt. This will occur at most, within 30 seconds, when the next outbound heartbeat occurs.
  centralHeartbeatLastReceived = millis();
}


void setMotionTimeout(char **values, int valueCount){
    /*
        Index 0 is the name of the command (in this case, 'set-motion-timeout')
        Index 1 is the new motion timeout in seconds
    */
    String motionTimeout = values[1];
    int newTimeout = motionTimeout.toInt();
    MOTION_TIMEOUT = newTimeout;
}

void setMotionDebugging(char**values,int valueCount){
  //A call to this should enable motion debugging until next restart. No
  //need for args.
  motionDebugging = true;
}


/* ************************************************************************************************
   ************************************************************************************************
                                           LED HANDLING
   ************************************************************************************************
   ************************************************************************************************
*/

/* Stores the current state of each led, in the order: north, south, east west
 * Valid values are
 'x' = off
 'o' = on
 'b' = breathing
 */
char ledStates[4] = {'x','x','x','x'};
//All LEDs share the same breathe value, so any leds that are in breathe mode are in sync.
int ledBreatheValue = 0; //Value of 0 to 255.  This needs to be mapped before being written to the pin.
int ledBreatheStep = 2;
int ledBreatheMin = 0;
int ledBreatheMax = 255;
void setLedBrightness(char **values, int valueCount){
    /*
        Index 0 is the name of the command (in this case, 'set-led-brightness')
        Index 1 is the new maximum brightness from 1 to 100. This affects all leds and their brightness. Use map() function to compute the new ranges.
    */
    String brightnessString = values[1];
    int newBrightness = brightnessString.toInt();
    MAX_BRIGHTNESS = newBrightness;
}

void setLedState(char **values, int valueCount){
    /*
        Index 0 is the name of the command (in this case, 'set-led-state')
        Index 1 is the led identifier (north/south/east/west)
        Index 2 is on,  off or breathe;
    */
    int ledIndex = 0;
    String ledIdentifier = values[1];
    String ledStateString = values[2];
    char ledStateChar = 'z';
    if(ledStateString == "on"){
        ledStateChar = 'o';
    }
    else if (ledStateString == "off"){
        ledStateChar = 'x';
    }
    else if (ledStateString == "breathe"){
        ledStateChar = 'b';
    }
    else
    {
        return; //Invalid message
    }

    if(ledIdentifier == "north"){
        ledStates[0] = ledStateChar;
    }
    else if(ledIdentifier == "south"){
        ledStates[1] = ledStateChar;
    }
    else if(ledIdentifier == "east"){
        ledStates[2] = ledStateChar;
    }
    else if(ledIdentifier == "west"){
        ledStates[3] = ledStateChar;
    }
    else
    {
        return; //Invalid message
    }

    // Serial.println("LED States: (NSEW)");
    // for(int x = 0; x<= sizeof(ledStates);x++){
    //   Serial.println(ledStates[x]);
    // }
}

void breatheLed(char **values, int valueCount){
    Serial.println("Received a breathe led command");
}

void startupLedSequence(){
    for(int x = 0;x < 7; x++){
        digitalWrite(LED_EAST,LOW);
        digitalWrite(LED_NORTH,HIGH);
        delay(100);
        digitalWrite(LED_NORTH,LOW);
        digitalWrite(LED_WEST,HIGH);
        delay(100);
        digitalWrite(LED_WEST,LOW);
        digitalWrite(LED_SOUTH,HIGH);
        delay(100);
        digitalWrite(LED_SOUTH,LOW);
        digitalWrite(LED_EAST,HIGH);
        delay(100);
    }
    digitalWrite(LED_EAST,LOW);
}


// int ledBreatheValue = 0; //Value of 0 to 255.  This needs to be mapped before being written to the pin.
// int ledBreatheStep = 5;
// int ledBreatheMin = 0;
// int ledBreatheMax = 255;

void updateLeds()
{
    // ledStates exist in the array ledStates[]. Update the leds on each cycle here.
    //Note: analogWrite is a value between 0 and 255.

    //Breathing value is updated regardless of whether any leds are currently breathing.
    ledBreatheValue = ledBreatheValue + ledBreatheStep;
    if(ledBreatheValue <= ledBreatheMin || ledBreatheValue >= ledBreatheMax){
        ledBreatheStep = -ledBreatheStep;
    }

    //Tidy up limits
    ledBreatheValue = (ledBreatheValue > 255) ? 255 : ledBreatheValue;
    ledBreatheValue = (ledBreatheValue < 1) ? 1 : ledBreatheValue;

    //Ensure breathing effect aheres to max brightess setting.
    int mappedMaxBrightness = map(MAX_BRIGHTNESS,0,99,0,255);
    int mappedBreatheValue = map(ledBreatheValue,1,255,1,MAX_BRIGHTNESS);
    //ledBreatheValue =

        // Serial.print(ledBreatheValue);
        // Serial.print("\t");
        // Serial.println(mappedBreatheValue);

    //North LED:
    if(ledStates[0] == 'x'){
        analogWrite(LED_NORTH,0); //off
    }
    else if(ledStates[0] == 'o'){
        analogWrite(LED_NORTH,map(255,0,255,0,MAX_BRIGHTNESS));
    }
    else if(ledStates[0] == 'b'){
        //Update breathe value
        analogWrite(LED_NORTH,mappedBreatheValue);
    }


    //South LED:
    if(ledStates[1] == 'x'){
        analogWrite(LED_SOUTH,0); //off
    }
    else if(ledStates[1] == 'o'){
        analogWrite(LED_SOUTH,map(255,0,255,0,MAX_BRIGHTNESS));
    }
    else if(ledStates[1] == 'b'){
       analogWrite(LED_SOUTH,mappedBreatheValue);
    }


    //East LED:
    if(ledStates[2] == 'x'){
        analogWrite(LED_EAST,0); //off
    }
    else if(ledStates[2] == 'o'){
        analogWrite(LED_EAST,map(255,0,255,0,MAX_BRIGHTNESS));
    }
    else if(ledStates[2] == 'b'){
        analogWrite(LED_EAST,mappedBreatheValue);
    }


    //West LED:
    if(ledStates[3] == 'x'){
        analogWrite(LED_WEST,0); //off
    }
    else if(ledStates[3] == 'o'){
        analogWrite(LED_WEST,map(255,0,255,0,MAX_BRIGHTNESS));
    }
    else if(ledStates[3] == 'b'){
       analogWrite(LED_WEST,mappedBreatheValue);
    }


}




//Like throwing an exception. Flashes the leds constantly to signal a coding error. Device must be reset to exit panic mode.
void panic(){
    while(true){
        digitalWrite(LED_EAST,LOW);
        digitalWrite(LED_NORTH,HIGH);
        delay(100);
        digitalWrite(LED_WEST,LOW);
        digitalWrite(LED_SOUTH,HIGH);
        delay(100);
        digitalWrite(LED_NORTH,LOW);
        digitalWrite(LED_WEST,HIGH);
        delay(100);
        digitalWrite(LED_SOUTH,LOW);
        digitalWrite(LED_EAST,HIGH);
        delay(100);
    }
}


void sendMessage(String message){
    Serial.print("Sending message '");
    Serial.print(message);
    Serial.println("'");

    //Ensure we don't have a half open socket. Check if we've received a heartbeat from central recently
    if(centralHeartbeatLastReceived + centralHeartbeatInterval < millis()){
      //We haven't received a heartbeat. Close the connection here, so it's restablished below.
      client.stop();
      centralHeartbeatLastReceived = millis(); //Prevents an infinite loop of disconnects.
    }

     while(!client.connected()){
        //digitalWrite(LED_TEST,HIGH);
        client.stop();
        delay(700);
        Serial.println("Reconnecting...");
        client.connect(server,port);

        if(!client.connected()){
            //Connection attempt timed out
            connectionRetries++;
            if(connectionRetries >= connectionRetryLimit){
                //Hmmm, either central control is down, or the spark core is acting up, try restarting the CC3000 (WiFi chip)
                  RGB.control(false);

                  WiFi.disconnect();
                  WiFi.off();
                  delay(500);
                  WiFi.on();
                  delay(500);
                  WiFi.connect();
                  while(!WiFi.ready()){
                    delay(100);
                  }

                  RGB.control(true);
                  RGB.brightness(0);
                  connectionRetries = 0;
            }
        }
        else{
            //Connection successful
            connectionRetries = 0;
            Serial.println("Connection established");
        }
    }
    //digitalWrite(LED_TEST,LOW);
    client.print(deviceIdentifier);
    client.print(":");
    client.println(message);
}

//Checks if data is available from network receives it into a buffer.
void receiveData(){
    if(client.connected() && client.available()){
        char c = client.read();
        commandParser.appendChar(c);
    }
}





void checkIn(){

    if(nextCheckIn < millis()){
        Serial.println("Sending heartbeat in...");
        sendMessage("heartbeat");
        nextCheckIn = millis() + checkInInterval;
    }


}


void checkPot(){
    if(millis() > lastPotReadingTime + potReadingInterval) //check max. 5 times per second
    {
        lastPotReadingTime = millis();
        int rawValue = analogRead(POT_PIN);
        int normalized;
        if(deviceIdentifier == "bedside"){
          //The bedside device seems to have an inverted pot. No matter, this fixes it by reversing the mapping
          normalized = constrain(map(rawValue, 0, 4095, 105, 0),0,100);
        }
        else{
          normalized = constrain(map(rawValue, 0, 4095, 0, 105),0,100);
        }

        //Round off values close to the edges to ensure we get the full range, eg: can access full brightness.
        if(normalized >= 92)
        {
            normalized = 100;
        }
        else if(normalized <= 4)
        {
            normalized = 0;
        }

        String normalizedString = String(normalized);
        if(normalized != lastPotReadingValue &&
            (normalized - lastPotReadingValue >= potReadingStep) || (lastPotReadingValue - normalized >= potReadingStep))
        {
            // The pot value has changed, we only update if the value changed, so send message to central
            String message = "pot-" + normalizedString;
            sendMessage(message);
            lastPotReadingValue = normalized;
        }
    }
}




void northClick(){
    Serial.println ("Button pressed");
    digitalWrite(LED_NORTH,MAX_BRIGHTNESS);
    sendMessage("button-north-pressed");
    digitalWrite(LED_NORTH,LED_OFF);
}

void northDoubleClick(){
    digitalWrite(LED_NORTH,MAX_BRIGHTNESS);
    sendMessage("button-north-double-pressed");
    digitalWrite(LED_NORTH,LED_OFF);
}

void northLongClick(){
    digitalWrite(LED_NORTH,MAX_BRIGHTNESS);
    sendMessage("button-north-long-pressed");
    digitalWrite(LED_NORTH,LED_OFF);
}



void southClick(){
    digitalWrite(LED_SOUTH,MAX_BRIGHTNESS);
    sendMessage("button-south-pressed");
    digitalWrite(LED_SOUTH,LED_OFF);
}

void southDoubleClick(){
    digitalWrite(LED_SOUTH,MAX_BRIGHTNESS);
    sendMessage("button-south-double-pressed");
    digitalWrite(LED_SOUTH,LED_OFF);
}

void southLongClick(){
    digitalWrite(LED_SOUTH,MAX_BRIGHTNESS);
    sendMessage("button-south-long-pressed");
    digitalWrite(LED_SOUTH,LED_OFF);
}



void eastClick(){
    digitalWrite(LED_EAST,MAX_BRIGHTNESS);
    sendMessage("button-east-pressed");
    digitalWrite(LED_EAST,LED_OFF);
}

void eastDoubleClick(){
    digitalWrite(LED_EAST,MAX_BRIGHTNESS);
    sendMessage("button-east-double-pressed");
    digitalWrite(LED_EAST,LED_OFF);
}

void eastLongClick(){
    digitalWrite(LED_EAST,MAX_BRIGHTNESS);
    sendMessage("button-east-long-pressed");
    digitalWrite(LED_EAST,LED_OFF);
}



void westClick(){
    digitalWrite(LED_WEST,MAX_BRIGHTNESS);
    sendMessage("button-west-pressed");
    digitalWrite(LED_WEST,LED_OFF);
}

void westDoubleClick(){
    digitalWrite(LED_WEST,MAX_BRIGHTNESS);
    sendMessage("button-west-double-pressed");
    digitalWrite(LED_WEST,LED_OFF);
}

void westLongClick(){
    digitalWrite(LED_WEST,MAX_BRIGHTNESS);
    sendMessage("button-west-long-pressed");
    digitalWrite(LED_WEST,LED_OFF);
}



void touchSense(){

  CapTouch::Event touchEvent = Touch.getEvent();

  if (touchEvent == CapTouch::TouchEvent) {
    Serial.println("ON ******");
  } else if (touchEvent == CapTouch::ReleaseEvent) {
    Serial.print("OFF -");
  }
}




void loop() {
    // This method loops forever...
    if(deviceIdentifier == "bathroom"){
      northButton.tick();
      updateMotion(1);
    }
    else {
      northButton.tick();
      southButton.tick();


      if(deviceIdentifier == "kitchen"){
        updateMotion(3);
      }
      else{
        checkPot();
        eastButton.tick();
        westButton.tick();
      }
    }

    updateLeds();
    checkIn();
    receiveData();
    delay(10); //Buttons get all weird if this delay isn't here
}
