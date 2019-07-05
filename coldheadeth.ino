#include <MCP320X.h>
#include <ETH.h>

//Define ADC interfaces
#define NCP3204 4
MCP320X mcp320

//Define network settings
static bool eth_connected = false;
IPAddress ip(10, 0, 0, 40);
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
  
  Wire.begin();
  delay(1000);
  //uint8_t status;
  for(uint8_t j = 0; j < 2; j++){
    for(uint8_t i = 0; i < N_Mag; i++){
      Serial.println(mlx[i].begin(i/2, i%2)); // 255 return is BAD!!!
      delay(500);
    }
  }
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
    // GET option, various mlx configurations
    case 'G':
      GetConfig(client, input);
      break;
    // READ option, <RN>, return magnetometer N value
    case 'R':
      measurement(client, input[1]-'0'); // expect a uint8_t
      break;
    // SET option, various mlx configurations
    case 'S':
      SetConfig(client, input);
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

// function to test for valid magID
uint8_t TestMagID(WiFiClient *client, uint8_t magID)
{
  if (magID>N_Mag-1) {
    client->println("ERROR: Invalid magID");
    return 255;
  }
  return 0;
}

// function to parse get commands and format replies
void GetConfig(WiFiClient *client, char *input)
{
  uint8_t magID = input[2]-'0';
  if (TestMagID(client, magID)) return;
  switch(input[1]) {
    // GET GAIN option, <GGN>, get magnetometer N gain
    case 'G':
      uint8_t gain;
      mlx[magID].getGainSel(gain);
      client->println(gain);
      break;
    // GET RESOLUTION option, <GRN>, get magnetometer N resolution for 3 axes
    case 'R':
      uint8_t res_x, res_y, res_z;
      mlx[magID].getResolution(res_x, res_y, res_z);
      client->print("[");
      client->print(res_x);
      client->print(",");
      client->print(res_y);
      client->print(",");
      client->print(res_z);
      client->println("]");
      break;
    default:
      client->println("ERROR: Invalid GET command");
      break;
  }
}

// function to parse set commands and format replies
void SetConfig(WiFiClient *client, char *input)
{
  uint8_t magID = input[2]-'0';
  if (TestMagID(client, magID)) return;
  switch(input[1]) {
    // SET GAIN option, <GGN Y>, set magnetometer N gain to value Y
    case 'G':
      client->println(mlx[magID].setGainSel(input[4]-'0'));
      break;
    // SET RESOLUTION option, <SRN XYZ>, set magnetometer N resolution to XYZ value for three axes
    case 'R':
      client->println(mlx[magID].setResolution(input[4]-'0',input[5]-'0',input[6]-'0'));
      break;
    default:
      client->println("ERROR: Invalid SET command");
      break;
  }
}

// function to record measurements from a magnetometer
void measurement(WiFiClient *client, uint8_t magID)
{
  if (TestMagID(client, magID)) return;
  MLX90393::txyz data;
  mlx[magID].readData(data);
  client->print("[");
  client->print(data.x);
  client->print(",");
  client->print(data.y);
  client->print(",");
  client->print(data.z);
  client->print(",");
  client->print(data.t);
  client->println("]");
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
