#ifndef yt_touch_command_H_
#define yt_touch_command_H_

#define MAX_COMMANDS 30
#define LOWER_TH 0
#define UPPER_TH 1

#define TOUCH_TH_DISABLED -1

#define TOUCH_CMD_CONFIRM_IDLE 0
#define TOUCH_CMD_CONFIRM_WAITING 1
#define TOUCH_CMD_CONFIRM_CONFIRMED 2
#define TOUCH_CMD_CONFIRM_TIMEDOUT 3

typedef struct {
  uint8_t id;               // command id
  uint8_t triggerOnTH;      // LOWER_TH - lower, UPPER_TH - upper.
  uint16_t touchDurationMs; // wait period before this command triggers
  uint16_t timeoutPeriodMs; // period after touchDurationMs elapsed. Use to expire command.
  uint8_t confirm;          // confirmation flag - touch released within command period.
} command_t;

class TouchCommand {
public:
  TouchCommand(
    void (*fnCb)(command_t *pCmd),
    uint8_t sensorPinId,
    uint16_t readIntervalMs,
    uint16_t samplingRate,
    uint8_t lowerTresholdTriggerValue,
    uint8_t upperTresholdTriggerValue = TOUCH_TH_DISABLED
  ):
    m_fnCbTriggered(fnCb),
    m_sensorPinId(sensorPinId),
    m_readIntervalMs(readIntervalMs),
    m_samplingRate(samplingRate),
    m_lowerThTriggerValue(lowerTresholdTriggerValue),
    m_upperThTriggerValue(upperTresholdTriggerValue)
  {
    m_commandLen = 0;
    memset(m_commands, '\0', sizeof(command_t) * MAX_COMMANDS);
  }

  void setCommands(command_t **pCmds, size_t len) {
    assert(len && pCmds);
    assert(MAX_COMMANDS >= len);
    m_commandLen = len;
    for (int x = 0; x < m_commandLen; x++) {
      m_commands[m_commandLen++] = pCmds[x];
    }
  }

  void addCommand(command_t *pCmd) {
    assert(pCmd);
    assert(MAX_COMMANDS >= m_commandLen);
    m_commands[m_commandLen++] = pCmd;
  }

  void listen()
  {
    uint32_t elapsedMs = 0;
    command_t *pCmdWaitingOnConfirm = NULL;

    uint16_t touchDurationMs = 0;
    uint16_t timeoutPeriodMs = 0;

    while(true)
    {
      float sensorValue = 0;
      for (int x = 0; x < m_samplingRate; x++) {
        // read sensor value of Tx(m_sensorPinId)
        sensorValue += touchRead(m_sensorPinId);
      }
      sensorValue /= 100; // extract average value.
      // Check if touched
      if (
        (TOUCH_TH_DISABLED != m_lowerThTriggerValue && sensorValue <= m_lowerThTriggerValue) ||
        (TOUCH_TH_DISABLED != m_upperThTriggerValue && sensorValue >= m_upperThTriggerValue)
      ) {
        Serial.printf("Touch (T5) active! Value: %00.2f\n", sensorValue);
        for (int x = 0; x < m_commandLen; x++)
        {
          touchDurationMs = m_commands[x]->touchDurationMs - 1000;
          timeoutPeriodMs = m_commands[x]->timeoutPeriodMs - 1000;
          if (
            LOWER_TH == m_commands[x]->triggerOnTH &&
            touchDurationMs <= elapsedMs &&
            timeoutPeriodMs >= elapsedMs
          ) {
            pCmdWaitingOnConfirm = m_commands[x];
          } else if (
            UPPER_TH == m_commands[x]->triggerOnTH &&
            touchDurationMs >= elapsedMs &&
            timeoutPeriodMs >= elapsedMs
          ) {
            pCmdWaitingOnConfirm = m_commands[x];
          }
          if (pCmdWaitingOnConfirm) {
            Serial.printf("triggerred touch! id: %d, triggerOnEvnt: %d, triggerTH: %d, timeoutTH: %d, elapsedMs: %d\n",
              m_commands[x]->id,
              m_commands[x]->triggerOnTH,
              m_commands[x]->touchDurationMs,
              m_commands[x]->timeoutPeriodMs,
              elapsedMs + 1000);
            // set touch status
            if (
              timeoutPeriodMs == elapsedMs &&
              TOUCH_CMD_CONFIRM_WAITING == pCmdWaitingOnConfirm->confirm
            ) {
              pCmdWaitingOnConfirm->confirm = TOUCH_CMD_CONFIRM_TIMEDOUT;
            } else if (pCmdWaitingOnConfirm->confirm == TOUCH_CMD_CONFIRM_IDLE) {
              pCmdWaitingOnConfirm->confirm = TOUCH_CMD_CONFIRM_WAITING;
            }
            // we hit treshold, let app know - process trigger
            m_fnCbTriggered(pCmdWaitingOnConfirm);
            if (TOUCH_CMD_CONFIRM_TIMEDOUT == pCmdWaitingOnConfirm->confirm) {
              // reset for the next command
              pCmdWaitingOnConfirm->confirm = TOUCH_CMD_CONFIRM_IDLE;
              pCmdWaitingOnConfirm = NULL;
            }
          }
        }
        // continue to next cycle
        elapsedMs += m_readIntervalMs;
      } else {
        elapsedMs = 0;
        if (pCmdWaitingOnConfirm && TOUCH_CMD_CONFIRM_WAITING == pCmdWaitingOnConfirm->confirm) {
          // touch released after a command was triggered.
          pCmdWaitingOnConfirm->confirm = TOUCH_CMD_CONFIRM_CONFIRMED;
          // process trigger
          m_fnCbTriggered(pCmdWaitingOnConfirm);
          // reset states
          pCmdWaitingOnConfirm->confirm = TOUCH_CMD_CONFIRM_IDLE;
          pCmdWaitingOnConfirm = NULL;
        }
      }
      // sleep before next touch read.
      delay(m_readIntervalMs);
    }
  }

private:
  void (*m_fnCbTriggered)(command_t *cmd);

  uint8_t m_sensorPinId;
  uint16_t m_readIntervalMs;
  uint8_t m_lowerThTriggerValue;
  uint8_t m_upperThTriggerValue;
  uint16_t m_samplingRate;

  size_t m_commandLen;
  command_t *m_commands[MAX_COMMANDS];
};

#endif // yt_touch_command_H_
