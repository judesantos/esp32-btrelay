
#include <BTHID.h>
#include <usbhub.h>
#include "KeyboardParser.h"
#include "MouseParser.h"
#include <SPI.h>

#if CONFIG_FREERTOS_UNICORE
  #define ARDUINO_RUNNING_CORE 0
#else
  #define ARDUINO_RUNNING_CORE 1
#endif

bool pairingInProgress = false;
void pairingComplete();

USB Usb;
BTD Btd(&Usb); 
BTHID *pBtHid = NULL;

KbdRptParser keyboardPrs;
MouseRptParser mousePrs;

const char *securityPin = "0000";

void initializeHID(bool pair = false) 
{
  Serial.printf("\nENTER initializeHID\n");
  Serial.printf("mode:  %s\n", pair ? "pair" : "connect");
  pairingInProgress = false;

  if (pBtHid)
  {
    pBtHid->disconnect();
    delete pBtHid; // discard old instance
    pBtHid = NULL;
  }
   
  pBtHid = pair ? new BTHID(&Btd, pair, securityPin) : new BTHID(&Btd);
  
  if (pair) {
    pairingInProgress = true;
  }
  
  pBtHid->attachOnInit(pairingComplete);
  pBtHid->SetReportParser(KEYBOARD_PARSER_ID, &keyboardPrs);
  pBtHid->SetReportParser(MOUSE_PARSER_ID, &mousePrs);
  // If "Boot Protocol Mode" does not work, then try "Report Protocol Mode"
  // If that does not work either, then uncomment PRINTREPORT in BTHID.cpp to see the raw report
  pBtHid->setProtocolMode(USB_HID_BOOT_PROTOCOL); // Boot Protocol Mode
  //pBtHid->setProtocolMode(HID_RPT_PROTOCOL); // Report Protocol Mode

}

void pairingComplete() 
{
  Serial.printf("\n**** Pairing is complete *****\n");
  pairingInProgress = false;
  initializeHID(); // paired mode
}

/**
 * multi-tasking (2nd core) process 
 * 
 */
void sensorLoop(void *params) 
{
  /**************************************************************************
   * 
   * Wait for user commands - touch duration actions
   * 
   *  1. pair mode - authenticate and add new device (3 seconds).
   *  2. connect mode/cancel pair mode - connect paired devices (3 seconds).
   *  3. system restart (10 seconds).
   *  
   **************************************************************************/ 
  // interval before next sensor read.
  const uint16_t touchReadIntervalMs = 1000;
  // initiate system restart when touch held for 9 of touchReadIntervalMs seconds
  const uint16_t restartSwitchThMs = 10 * touchReadIntervalMs;
  // set flag to switch/cancel bluetooth pairing mode when touch is 
  // held for 3 of touchReadIntervalMs seconds
  const uint16_t initiateBTDevicePairingThMs = 3 * touchReadIntervalMs;
  // max touch period before ignoring the pairing request.
  const uint16_t maxWaitTouchReleasePairingRequestMs = 6 * touchReadIntervalMs;
  // touch capacitance level treshold - trigger action if value goes lower than touchTreshold
  const uint8_t touchTreshold = 15;
  // length of continued touch
  uint16_t touchElapsedMs = 0;
  // Toggle pairing mode - we got a previous warning to start/stop pairing mode. 
  // When initiateBTDevicePairingThMs is TRUE, cycle some more waiting for user 
  // to release touch and start/cancel pairing.
  // if set to TRUE, user released touch and confirms recent requested action.
  // Cancel pairing occurs when pairing mode is in effect.
  // If user does not release touch after the specified max duration - 
  // maxWaitTouchReleasePairingRequestMs, no action is taken and timer is reset
  // to wait for next command, if any.
  bool initiatePairingMode = false;
  // wait for commands
  while(true)
  {
    float sensorValue = 0;
    // sampling rate at 100x
    for (int x = 0; x < 100; x++) {
      // read sensor value of Touch5 (GPIO12, Pin12)
      sensorValue += touchRead(T5);
    }
    sensorValue /= 100;
    Serial.printf("\ntouch sense (T5) value: %00.2f", sensorValue); 
    if (sensorValue < touchTreshold) 
    {
      if (restartSwitchThMs <= touchElapsedMs) 
      {
        // restart device
        Serial.println("\nUser initiated reset. Restarting in 4 seconds...\n");
        delay(4000);
        // restart now!
        ESP.restart();
        break;
      }
      else if (
        initiateBTDevicePairingThMs <= touchElapsedMs && 
        maxWaitTouchReleasePairingRequestMs >= touchElapsedMs
      ) {
        if (!initiatePairingMode) {
          Serial.printf("\nPairing mode %s", pairingInProgress ? "'cancel'" : "'enable'");
          initiatePairingMode = true;
        } else {
          // cancel if user did not release touch for another 3 seconds.
          // user is possibly initiating reboot.
          if (touchReadIntervalMs * 7 == touchElapsedMs) {
            if (initiatePairingMode) {
              Serial.println("\nPairing mode canceled");
              initiatePairingMode = false;
            }           
          }
        }
      }      
      // continue to next cycle
      touchElapsedMs += touchReadIntervalMs;     
    } 
    else 
    {
      touchElapsedMs = 0;
      if (initiatePairingMode) {
        initiatePairingMode = false;
        // toggle pairing mode
        initializeHID(pairingInProgress ? false : true); 
      }
    }
    // sleep touchReadIntervalMs seconds
    delay(touchReadIntervalMs);
    // awake! Get next value...
  }
}

void setup() 
{
  Serial.begin(115200);
  // multi-task other services
  xTaskCreatePinnedToCore(sensorLoop, "sensorTask", 6144, NULL, 1, NULL, ARDUINO_RUNNING_CORE);
  // start USB
  if (Usb.Init() == -1) {
    Serial.print(F("\r\nOSC did not start"));
    while (1); // Halt
  }
  initializeHID(); // connect mode
  Serial.print(F("\r\nHID Bluetooth Library Started"));
}

void loop() 
{
  Usb.Task();
  delay(1);
}
