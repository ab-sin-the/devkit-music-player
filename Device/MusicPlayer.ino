#include "mbed.h"
#include "AZ3166WiFi.h"
#include "Arduino.h"
#include "http_client.h"
#include "AudioClassV2.h"
#include "stm32412g_discovery_audio.h"
#include "RingBuffer.h"
#include "SystemTickCounter.h"
#include "WebSocketClient.h"
#include "AzureIotHub.h"
#include "mbed_memory_status.h"
#include "DevKitMQTTClient.h"
#include "OledDisplay.h"
#include "parson.h"
#include "music.h"
#include "utils.h"
#include "WebSocketUtils.h"
#include "UARTClass.h"

const int SAMPLE_RATE = 8000;
const int SAMPLE_BIT_DEPTH = 16;

static AudioClass &Audio = AudioClass::getInstance();
RingBuffer ringBuffer(RING_BUFFER_SIZE);
char readBuffer[AUDIO_CHUNK_SIZE];
static char emptyAudio[AUDIO_CHUNK_SIZE];
UARTClass Serial1 = UARTClass(UART_1);



static long playProgress = 0;
static long musicNumberPlayed = 0;
bool receivedNewIDFromHub = false;
bool firstTimeStart = true;

char *sendMsg = NULL;
bool startPlay = false;
Music current_music;

#define STOP_WORD_IDX 5
#define PLAY_WORD_IDX 3
#define JUMP_WORD_IDX 2

void initWiFi() {
    Screen.print("IoT DevKit\r\n \r\nConnecting...\r\n");

    if (WiFi.begin() == WL_CONNECTED) {
        IPAddress ip = WiFi.localIP();
        Screen.print(1, ip.get_address());
        hasWifi = true;
        Screen.print(2, "Running... \r\n");
    } else {
        Screen.print(1, "No Wi-Fi\r\n ");
    }
}

void play() {
    enterPlayingState();
    Audio.startPlay(playCallback);
    startPlay = true;
}

void stop() {
    printf("stop()\n");
    Audio.stop();
    Audio.writeToPlayBuffer(emptyAudio, AUDIO_CHUNK_SIZE);
    startPlay = false;
}

void playCallback(void) {
    if (ringBuffer.use() < AUDIO_CHUNK_SIZE) {
        Audio.writeToPlayBuffer(emptyAudio, AUDIO_CHUNK_SIZE);
        return;
    }
    ringBuffer.get((uint8_t*)readBuffer, AUDIO_CHUNK_SIZE);
    Audio.writeToPlayBuffer(readBuffer, AUDIO_CHUNK_SIZE);
}

void setResponseBodyCallback(const char *data, size_t dataSize) {
    while (ringBuffer.available() < dataSize) {
        delay(10);
    }

    ringBuffer.put((uint8_t*) data, dataSize);
    if (ringBuffer.use() > RING_BUFFER_SIZE * PLAY_DELAY_RATE && startPlay == false) {
        play();
    }
}

void enterIdleState() {
    status = INIT_STATE;
    Screen.clean();
    Screen.print(0, "Press A to start...");
}

void enterGetIDState() {
    status = REQUESTING_MUSIC_STATE;
    Screen.clean();
    Screen.print(0, "Start Playing");
}

void enterRequestingMusicState() {
    status = REQUESTING_MUSIC_STATE;
    Screen.clean();
    Screen.print(0, "Requesting...");
}

void enterPlayingState() {
    status = PLAYING_STATE;
    Screen.clean();
    Screen.print(0, "Playing");

}
bool playComplete = true;
void enterJumpNextState() {
    Screen.clean();
    Screen.print(0, "Jumping to next");
    playComplete = true;
    enterGetIDState();
}
void enterStopState() {
    status = STOP_STATE;
    Screen.print(0, "Stopped...");
}
void getMusicBuffer(long musicID) {
    if (!isWsConnected) {
        isWsConnected = connectWebSocket();
    }
    sendNext();

    //memcpy(websocketBuffer, &musicID, sizeof(long));
   // memcpy(websocketBuffer + sizeof(long), &playProgress, sizeof(long));
   // wsClient -> send(websocketBuffer, sizeof(long) * 2, WS_Message_Binary, true);
}
void setup() {
    Screen.init();
    Screen.print(0, "IoT DevKit");
    Screen.print(2, "Initializing...");

    Screen.print(3, " > Serial");
    // Initialize the WiFi module
    Screen.print(3, " > WiFi");
    hasWifi = false;
    initWiFi();
    if (!hasWifi) {
        return;
    }

    memset(emptyAudio, 0x0, AUDIO_CHUNK_SIZE);
    pinMode(USER_BUTTON_A, INPUT);
    pinMode(USER_BUTTON_B, INPUT);
    
    Serial.begin(115200);
    Serial1.begin(115200);
    if (!isWsConnected) {
        isWsConnected = connectWebSocket();
    }
    if (!isWsConnected) return;
    enterIdleState();
    //Audio.format(SAMPLE_RATE, SAMPLE_BIT_DEPTH);
    Audio.setVolume(30);
}

void loop() {
     if (hasWifi) {
        doWork();
    }
}

void doWork() {
    switch (status) {
        // Idle
    case INIT_STATE:
        {
            buttonAState = digitalRead(USER_BUTTON_A);
            if (buttonAState == LOW) {
                enterGetIDState();
            }
            break;
        }
        // Receiving and playing
    case REQUESTING_MUSIC_STATE:
        {
            while (Serial1.read() != -1) {
                
            }
            getMusicBuffer(current_music.music_id);
            printf("Play progress:%d\n", playProgress);
            bool isEndOfMessage = false;
            WebSocketReceiveResult *recvResult = NULL;
            int len = 0;
            printf("Receiving message\n");
            printf("Total music played: %d\n", musicNumberPlayed);
            while (!isEndOfMessage) { 
            
                int incomingByte = Serial1.read();
                if (incomingByte == STOP_WORD_IDX) {
                // if (digitalRead(USER_BUTTON_A) == LOW) {
                //     do {
                //     } while (digitalRead(USER_BUTTON_A) == LOW);
                    enterStopState();
                    stop();
                    playComplete = false;
                    break;
                } 
                //else if (incomingByte == JUMP_WORD_IDX) {
                 else if (digitalRead(USER_BUTTON_B) == LOW) {
                     do {
                     } while (digitalRead(USER_BUTTON_B) == LOW);
                    stop();
                    enterJumpNextState();
                    break;
                } else {
                    recvResult = wsClient -> receive(websocketBuffer, sizeof(websocketBuffer));
                    if (recvResult != NULL && recvResult -> length > 0) {
                        len = recvResult -> length;
                        isEndOfMessage = recvResult -> isEndOfMessage;
                        setResponseBodyCallback(websocketBuffer, len);
                        playProgress += len;
                    } else {
                        printf("Receive NULL, resend request\n");
                        delay(1000);
                        if (connectWebSocket()) {
                            printf("reconnect success\n");
                        }
                        playComplete = false;
                        break;
                    }
                }
            };

            memset(websocketBuffer, 0, sizeof(websocketBuffer));
            
            if (!playComplete) break;
            while (ringBuffer.use() >= AUDIO_CHUNK_SIZE) {
                printf("delaying\n");
                ringBuffer.get((uint8_t*)readBuffer, AUDIO_CHUNK_SIZE);
            }
            closeWebSocket();
            delete recvResult;
            enterGetIDState();
            break;
        }
    case STOP_STATE:
        {
            int incomingByte = Serial1.read();
            if (incomingByte == PLAY_WORD_IDX) {
                printf("Restart\n");
                enterRequestingMusicState();
                break;
            }
            else if (incomingByte == JUMP_WORD_IDX) {
                closeWebSocket();
                enterJumpNextState();
                break;
            }
            break;
        }
    }
}

