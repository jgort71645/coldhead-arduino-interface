#include <MCP3XXX.h>
#include <ETH.h>

//Define ADC interfaces
#define CS_PIN 4     // ESP8266 default SPI pins
#define CLK_PIN 13  // Should work with any other GPIO pins, since the library does not formally
#define MOSI_PIN 12   // use SPI, but rather performs pin bit banging to emulate SPI communication.
#define MISO_PIN 11   //
typedef MCP3XXX_<12, 4, 1000000> MCP3204;
MCP3204 adc;

//Define network settings
static bool eth_connected = false;
IPAddress ip(10, 0, 0, 39);
IPAddress gate(10, 0, 0, 1);
IPAddress mask(255, 255, 255, 0);

//Define telnet server parameters
#define MAX_SRV_CLIENTS 4
WiFiServer server(23);
WiFiClient serverClients[MAX_SRV_CLIENTS];
const byte charLim = 32;
const char terminator = '\n';

void WiFiEvent(WiFiEvent_t event)
{
  switch (event) {
    case SYSTEM_EVENT_ETH_START:
      Serial.println("ETH Started");
      //set eth hostname here
      ETH.setHostname("arduino1");
      break;
    case SYSTEM_EVENT_ETH_CONNECTED:
      Serial.println("ETH Connected");
      break;
    case SYSTEM_EVENT_ETH_GOT_IP:
      Serial.print("ETH MAC: ");
      Serial.print(ETH.macAddress());
      Serial.print(", IPv4: ");
      Serial.print(ETH.localIP());
      if (ETH.fullDuplex()) Serial.print(", FULL_DUPLEX");
      Serial.print(", ");
      Serial.print(ETH.linkSpeed());
      Serial.println("Mbps");
      eth_connected = true;
      break;
    case SYSTEM_EVENT_ETH_DISCONNECTED:
      Serial.println("ETH Disconnected");
      eth_connected = false;
      break;
    case SYSTEM_EVENT_ETH_STOP:
      Serial.println("ETH Stopped");
      eth_connected = false;
      break;
    default:
      break;
  }
}

void setup()
{
  Serial.begin(115200);

  WiFi.onEvent(WiFiEvent);
  ETH.begin();
  ETH.config(ip,gate,mask,gate);
  server.begin();
  server.setNoDelay(true);
  Serial.println("network and server setup initialized");
  adc.begin(CS_PIN, MOSI_PIN, MISO_PIN, CLK_PIN);
  Serial.println("setup done");
}

// function to handle new connections
void handle_connection()
{
  uint8_t i;
  if (server.hasClient()){
    for(i = 0; i < MAX_SRV_CLIENTS; i++){
      //find free/disconnected spot
      if (!serverClients[i] || !serverClients[i].connected()){
        if (serverClients[i]) serverClients[i].stop();
        serverClients[i] = server.available();
        if (!serverClients[i]) Serial.println("available broken");
        Serial.print("New client: ");
        Serial.print(i); Serial.print(' ');
        Serial.println(serverClients[i].remoteIP());
        serverClients[i].print("You are connection ");
        serverClients[i].print(i);serverClients[i].print(" to ");
        serverClients[i].println(ETH.getHostname());
        serverClients[i].print(">");
        //flush input buffer on connection
        while (serverClients[i].available()) serverClients[i].read();
        break;
      }
    }
    if (i >= MAX_SRV_CLIENTS) {
      //no free/disconnected spot so reject
      server.available().stop();
    }
  }
}

// function to read commands from controls
void read_connection()
{
  uint8_t i;
  for(i = 0; i < MAX_SRV_CLIENTS; i++){
    if (serverClients[i] && serverClients[i].connected()){
      if (serverClients[i].available()){
        char input[charLim+2];
        uint8_t nRx = 0;
        bool incomplete = true;
        while (serverClients[i].available()){
          input[nRx] = serverClients[i].read();
          if (input[nRx] == terminator) {
            input[nRx+2] = '\0';
            parse_command(&serverClients[i], input);
            incomplete = false;
            break;
          } else if (nRx < charLim) {
            nRx ++;
          }
        }
        if (incomplete) serverClients[i].println("ERROR: Incomplete transmission");
      }
    }
    else {
      if (serverClients[i]) serverClients[i].stop();
    }
  }
}

// funtion to interpret received command
void parse_command(WiFiClient *client, char *input)
{
  Serial.print("Received command: ");
  Serial.println(input);
  client->print(input);
  switch(input[0]) {
    // READ option, <RN>, return ADC input N value
    case 'R':
      measurement(client, input[1]-'0'); // expect a uint8_t
      break;
    // TEST option, <T> return 1
    case 'T':
      client->println(1);
      break;
    default:
      client->println("ERROR: Invalid command");
      break;
  }
  client->print(">");
}

// function to record measurements from a coldhead ADC
void measurement(WiFiClient *client, uint8_t chanID)
{
  if (chanID > 3) return;
  client->print("Ch: ");
  client->print(chanID);
  client->print("; Val: ");
  client->println(adc.analogRead(chanID));
}

void loop()
{
  if (eth_connected) {
    //check if there are any new clients
    handle_connection();
    //check clients for data
    read_connection();
  } else {
    Serial.println("ETH not connected!");
    delay(100);
    for(uint8_t i = 0; i < MAX_SRV_CLIENTS; i++) {
      if (serverClients[i]) serverClients[i].stop();
    }
  }
}
