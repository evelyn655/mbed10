#include "mbed.h"
#include "MQTTNetwork.h"
#include "MQTTmbed.h"
#include "MQTTClient.h"
#include "stm32l475e_iot01_accelero.h"

// GLOBAL VARIABLES
WiFiInterface *wifi;
//InterruptIn btn2(USER_BUTTON);
//InterruptIn btn3(SW3);
InterruptIn btnRecord(USER_BUTTON);
volatile int message_num = 0;
volatile int arrivedcount = 0;
volatile bool closed = false;


Thread mqtt_thread(osPriorityHigh);
Thread t;
EventQueue mqtt_queue;
EventQueue queue(32 * EVENTS_EVENT_SIZE);

int16_t pDataXYZ[3] = {0};
int idR[32] = {0};
int indexR = 0;

const char* topic = "Mbed";

void record(void) {
   BSP_ACCELERO_AccGetXYZ(pDataXYZ);
   printf("%d, %d, %d\n", pDataXYZ[0], pDataXYZ[1], pDataXYZ[2]);
}

void startRecord(void) {
   printf("---start---\n");
   idR[indexR++] = queue.call_every(1ms, record);
   indexR = indexR % 32;
}

void stopRecord(void) {
   printf("---stop---\n");
   for (auto &i : idR)
      queue.cancel(i);
}
 
void messageArrived(MQTT::MessageData& md) {
    MQTT::Message &message = md.message;
    char msg[300];
    sprintf(msg, "Message arrived: QoS%d, retained %d, dup %d, packetID %d\r\n", message.qos, message.retained, message.dup, message.id);
    //printf(msg);
    // 以上印標頭（訊息類型、DUP(是否為副本)、QoS(傳輸品質)、Retain(是否保留)）
    ThisThread::sleep_for(1000ms);
    char payload[300];
    sprintf(payload, "Payload %.*s\r\n", message.payloadlen, (char*)message.payload);
    //printf(payload);
    // 以上印 arrived 的東西
    ++arrivedcount;
}

void publish_message(MQTT::Client<MQTTNetwork, Countdown>* client) {
    message_num++;
    MQTT::Message message;
    char buff[100];
    //sprintf(buff, "QoS0 Hello, Python! #%d", message_num);
    sprintf(buff, "%d, %d, %d\n", pDataXYZ[0], pDataXYZ[1], pDataXYZ[2]);
    message.qos = MQTT::QOS0;
    message.retained = false;
    message.dup = false;
    message.payload = (void*) buff;
    message.payloadlen = strlen(buff) + 1;
    int rc = client->publish(topic, message);

    printf("rc:  %d\r\n", rc);
    printf("Puslish message: %s\r\n", buff);
}

void close_mqtt() {
    closed = true;
}

int main() {

    wifi = WiFiInterface::get_default_instance();
    if (!wifi) {
            printf("ERROR: No WiFiInterface found.\r\n");
            return -1;
    }


    printf("\nConnecting to %s...\r\n", MBED_CONF_APP_WIFI_SSID);
    int ret = wifi->connect(MBED_CONF_APP_WIFI_SSID, MBED_CONF_APP_WIFI_PASSWORD, NSAPI_SECURITY_WPA_WPA2);
    if (ret != 0) {
            printf("\nConnection error: %d\r\n", ret);
            return -1;
    }


    NetworkInterface* net = wifi;
    MQTTNetwork mqttNetwork(net);
    MQTT::Client<MQTTNetwork, Countdown> client(mqttNetwork);

    //TODO: revise host to your IP
    const char* host = "172.20.10.2";
    printf("Connecting to TCP network...\r\n");

    SocketAddress sockAddr;
    sockAddr.set_ip_address(host);
    sockAddr.set_port(1883);

    printf("address is %s/%d\r\n", (sockAddr.get_ip_address() ? sockAddr.get_ip_address() : "None"),  (sockAddr.get_port() ? sockAddr.get_port() : 0) ); //check setting

    int rc = mqttNetwork.connect(sockAddr);//(host, 1883);
    if (rc != 0) {
            printf("Connection error.");
            return -1;
    }
    printf("Successfully connected!\r\n");

    MQTTPacket_connectData data = MQTTPacket_connectData_initializer;
    data.MQTTVersion = 3;
    data.clientID.cstring = "Mbed";

    if ((rc = client.connect(data)) != 0){
            printf("Fail to connect MQTT\r\n");
    }
    if (client.subscribe(topic, MQTT::QOS0, messageArrived) != 0){
            printf("Fail to subscribe\r\n");
    }

    BSP_ACCELERO_Init();
    t.start(callback(&queue, &EventQueue::dispatch_forever));
    btnRecord.fall(queue.event(startRecord));
    btnRecord.rise(queue.event(stopRecord));

    mqtt_thread.start(callback(&mqtt_queue, &EventQueue::dispatch_forever));
    mqtt_queue.call_every(500, &publish_message, &client);                // call to publish message every 0.5 second
    //btn2.rise(mqtt_queue.event(&publish_message, &client));
    //btn3.rise(&close_mqtt);

    int num = 0;
    while (num != 5) {
        client.yield(100);

        // int yield	(unsigned long 	timeout_ms = 1000L )	
        // A call to this API must be made within the keepAlive interval to keep the MQTT connection alive yield can be called if no other MQTT operation is needed.

        // This will also allow messages to be received.

        // Parameters: timeout_ms	the time to wait, in milliseconds
        // Returns: success code - on failure, this means the client has disconnected
        ++num;
    }

    while (1) {
            if (closed) break;
            client.yield(500);
            ThisThread::sleep_for(500ms);
    }

    printf("Ready to close MQTT Network......\n");

    if ((rc = client.unsubscribe(topic)) != 0) {
            printf("Failed: rc from unsubscribe was %d\n", rc);
    }
    if ((rc = client.disconnect()) != 0) {
    printf("Failed: rc from disconnect was %d\n", rc);
    }

    mqttNetwork.disconnect();
    printf("Successfully closed!\n");

    return 0;
}