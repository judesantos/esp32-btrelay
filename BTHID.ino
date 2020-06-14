
#include <BTHID.h>
#include <usbhub.h>
#include "KeyboardParser.h"
#include "MouseParser.h"
#include <SPI.h>

#include "TouchCommand.h"

#if CONFIG_FREERTOS_UNICORE
  #define ARDUINO_RUNNING_CORE 0
#else
  #define ARDUINO_RUNNING_CORE 1
#endif

#define APP_COMMAND_ID_SHUTDOWN 0
#define APP_COMMAND_ID_PAIR 1

bool pairingInProgress = false;
void pairingComplete();

USB g_usb;
BTD g_btd(&g_usb);
BTHID *g_pBtHid = NULL;

KbdRptParser g_keyboardPrs;
MouseRptParser g_mousePrs;

const char *securityPin = "0000";

void initializeHID(bool pair = false)
{
  Serial.printf("\nENTER initializeHID\n");
  Serial.printf("mode:  %s\n", pair ? "pair" : "connect");
  pairingInProgress = false;
  if (g_pBtHid)
  {
    g_pBtHid->disconnect();
    delete g_pBtHid; // discard old instance
    g_pBtHid = NULL;
  }
  g_pBtHid = pair ? new BTHID(&g_btd, pair, securityPin) : new BTHID(&g_btd);
  if (pair) {
    pairingInProgress = true;
  }
  g_pBtHid->attachOnInit(pairingComplete);
  g_pBtHid->SetReportParser(KEYBOARD_PARSER_ID, &g_keyboardPrs);
  g_pBtHid->SetReportParser(MOUSE_PARSER_ID, &g_mousePrs);
  g_pBtHid->setProtocolMode(USB_HID_BOOT_PROTOCOL);
}

void pairingComplete()
{
  Serial.printf("\n**** Pairing is complete *****\n");
  pairingInProgress = false;
  initializeHID(); // paired mode
}

void touchCallback(command_t *pCmd) {
  switch(pCmd->id) {
    case APP_COMMAND_ID_SHUTDOWN:
      if (TOUCH_CMD_CONFIRM_CONFIRMED == pCmd->confirm)
      {
        // restart device
        Serial.println("User initiated reset. Restarting in 4 seconds...");
        // blink LED yellow
        delay(4000);
        // stop blink LED yellow
        // restart now!
        ESP.restart();
      } else if (TOUCH_CMD_CONFIRM_WAITING == pCmd->confirm) {
        Serial.println("User initiated reset. Restart Command, waiting to confirm...");
        // blink LED-RED
      } else if (TOUCH_CMD_CONFIRM_TIMEDOUT == pCmd->confirm) {
        Serial.println("User initiated reset. Restart command Timedout, Cancel Reset.");
        // stop blink LED-RED
      }
      break;
    case APP_COMMAND_ID_PAIR:
      if (TOUCH_CMD_CONFIRM_CONFIRMED == pCmd->confirm) {
        Serial.printf("Pairing mode %s", pairingInProgress ? "'cancel'" : "'activate'");
        // toggle pairing mode: if current state is pairing, cancel and switch to connect mode.
        initializeHID(pairingInProgress ? false : true);
      } else if (TOUCH_CMD_CONFIRM_WAITING == pCmd->confirm) {
        Serial.println("User initiated pair. Pair Command, waiting to confirm...");
        // blink LED blue
      } else if (TOUCH_CMD_CONFIRM_TIMEDOUT == pCmd->confirm) {
        Serial.println("User initiated pair. Pair command Timedout, Cancel Reset.");
        // stop blink LED blue
      }
      break;
    default:
      break;
  }
}

void sensorLoop(void *params)
{
  Serial.println("ENTER sensorLoop2");
  const uint16_t touchReadIntervalMs = 1000; // sleep before next read
  /*
   * create commands
   */
  Serial.println("Creating commands...");
  // device pairing command
  command_t cmdTogglePairingMode;
  cmdTogglePairingMode.id = APP_COMMAND_ID_PAIR;
  cmdTogglePairingMode.triggerOnTH = LOWER_TH;
  // trigger command when pressed 4sec. Release touch to confirm.
  cmdTogglePairingMode.touchDurationMs = 4 * touchReadIntervalMs;
  // cancel command when pressed to 7sec.
  cmdTogglePairingMode.timeoutPeriodMs = 7 * touchReadIntervalMs;
  cmdTogglePairingMode.confirm = TOUCH_CMD_CONFIRM_IDLE;
  // shutdown command
  command_t cmdShutDown;
  cmdShutDown.id = APP_COMMAND_ID_SHUTDOWN;
  cmdShutDown.triggerOnTH = LOWER_TH;
  // trigger shutdown when pressed 10sec. Release touch to confirm.
  cmdShutDown.touchDurationMs = 10 * touchReadIntervalMs;
  // cancel shutdown after pressed to 14sec.
  cmdShutDown.timeoutPeriodMs = 14 * touchReadIntervalMs;
  cmdShutDown.confirm = TOUCH_CMD_CONFIRM_IDLE;
  /*
   * create touch monitor object
   */
  Serial.println("Creating touch sensor...");
  const uint8_t touchLowerTreshold = 15;     // any reading lower than LowerTH indicates touched.
  const uint8_t sensorSamplingRate = 100;    // no. of times the sensor is read to get ave. value.
  //
  // Touch sensor at Touch5: check every 1sec, get 100 samples per read, derive ave. value;
  // trigger command if ave. value is less than 15.
  //
  TouchCommand touch(
    touchCallback,
    T5,
    touchReadIntervalMs,
    sensorSamplingRate,
    touchLowerTreshold
  );
  Serial.println("adding touch commands...");
  Serial.printf("Shutdown Command! id: %d, triggerTH: %d\n", cmdShutDown.id, cmdShutDown.touchDurationMs);
  Serial.printf("Pair Device Command! id: %d, triggerTH: %d\n", cmdTogglePairingMode.id, cmdTogglePairingMode.touchDurationMs);
  // add commands
  touch.addCommand(&cmdShutDown);
  touch.addCommand(&cmdTogglePairingMode);
  // blocking call - breaks on system restart.
  Serial.println("Touch, listening...");
  touch.listen();
  Serial.println("Touch, Exeunt!");
}

void setup()
{
  Serial.begin(115200);
  // USB Init
  if (g_usb.Init() == -1) {
    Serial.print(F("\nUSB Host Init Failed. Exeunt!"));
    while (1); // Halt
  }
  initializeHID(); // connect mode
  Serial.println("\nHID Started!!!");
  // async process - touch sensor
  xTaskCreatePinnedToCore(sensorLoop, "sensorTask", 6144, NULL, 1, NULL, ARDUINO_RUNNING_CORE);
}

void loop()
{
  g_usb.Task();
  delay(1);
}
