/*
 * top_up_station.ino - ESP32 bridge between STM32 top-up terminal and HiveMQ.
 *
 * Link protocol with the STM32 (over Serial2, 115200 8N1):
 *
 *   STM32 -> ESP32 :  CMD <action> <uid> [amount]
 *       actions: check_role | add_money | create_guest | remove_card | check_balance | ping
 *       e.g.  "CMD check_role A1B2C3D4"
 *             "CMD add_money A1B2C3D4 50"
 *             "CMD ping"                       (connection self-test)
 *
 *   ESP32 -> STM32 :  RESP <topic> OK|ERR [extra]
 *       e.g.  "RESP /check_role OK member"
 *             "RESP /add_money OK 50"
 *             "RESP /ping OK wifi=1,mqtt=1"     (reply to CMD ping)
 *             "RESP /ping ERR nowifi"
 *
 * Connection handling is non-blocking: WiFi and MQTT are (re)connected from
 * loop() without ever hanging, so UART commands are always serviced. The
 * on-board LED and periodic Serial logs report link health; a "CMD ping" lets
 * the STM32 check the whole chain on demand.
 */

#include <PubSubClient.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>

// --- UART2 to STM32 ---
// ESP32 RX2 (GPIO16) <- STM32 TX (PA2)
// ESP32 TX2 (GPIO17) -> STM32 RX (PA3)
#define RXD2 16
#define TXD2 17
#define LED_PIN 2 // on-board LED: solid = all up, blink = connecting

// --- Wi-Fi ---
const char			*ssid = "helloworld";
const char			*password = "25532548";

// --- HiveMQ Cloud ---
const char			*mqtt_server = "xc16c610.ala.asia-southeast1.emqxsl.com";
const int			mqtt_port = 8883;
const char			*mqtt_user = "chevfy";
const char			*mqtt_pass = "25532548";

// Topics the backend serves. We publish on these and subscribe to <topic>/response.
const char			*TOPIC_CHECK_ROLE = "/check_role";
const char			*TOPIC_ADD_MONEY = "/add_money";
const char			*TOPIC_CREATE_GUEST = "/create_guest";
const char			*TOPIC_REMOVE_CARD = "/remove_card";
const char			*TOPIC_CHECK_BALANCE = "/check_balance";

WiFiClientSecure	espClient;
PubSubClient		client(espClient);

// non-blocking timers
unsigned long		lastWifiTry = 0;
unsigned long		lastMqttTry = 0;
unsigned long		lastStatus = 0;
unsigned long		lastBlink = 0;
bool				ledState = false;

// ---------- small JSON helpers (no library needed) ----------

String	jsonStr(const String &src, const char *key)
{
	String	pat;
	int		i;
	int		start;
	int		end;

	pat = String("\"") + key + "\":\"";
	i = src.indexOf(pat);
	if (i < 0)
		return ("");
	start = i + pat.length();
	end = src.indexOf('\"', start);
	if (end < 0)
		return ("");
	return (src.substring(start, end));
}

String	jsonNum(const String &src, const char *key)
{
	String	pat;
	int		i;
	int		start;
	int		end;
	char	c;

	pat = String("\"") + key + "\":";
	i = src.indexOf(pat);
	if (i < 0)
		return ("");
	start = i + pat.length();
	end = start;
	while (end < (int)src.length())
	{
		c = src[end];
		if ((c >= '0' && c <= '9') || c == '-')
			end++;
		else
			break ;
	}
	return (src.substring(start, end));
}

// ---------- connection state ----------

bool	wifiUp(void)
{
	return (WiFi.status() == WL_CONNECTED);
}
bool	mqttUp(void)
{
	return (client.connected());
}

const char	*wifiStatusStr(wl_status_t s)
{
	switch (s)
	{
	case WL_IDLE_STATUS:
		return ("IDLE");
	case WL_NO_SSID_AVAIL:
		return ("NO_SSID_AVAIL (not found / 5GHz / wrong name)");
	case WL_SCAN_COMPLETED:
		return ("SCAN_COMPLETED");
	case WL_CONNECTED:
		return ("CONNECTED");
	case WL_CONNECT_FAILED:
		return ("CONNECT_FAILED (wrong password?)");
	case WL_CONNECTION_LOST:
		return ("CONNECTION_LOST");
	case WL_DISCONNECTED:
		return ("DISCONNECTED");
	default:
		return ("UNKNOWN");
	}
}

// One-shot scan of 2.4 GHz networks the ESP32 can actually see.
void	scanWifi(void)
{
	int		n;
	bool	seen;

	Serial.println("[scan] scanning 2.4GHz networks...");
	n = WiFi.scanNetworks();
	if (n <= 0)
	{
		Serial.println("[scan] NO networks found!");
		return ;
	}
	seen = false;
	for (int i = 0; i < n; i++)
	{
		Serial.printf("[scan] %2d) \"%s\"  ch%d  %ddBm  %s\n", i + 1,
			WiFi.SSID(i).c_str(), WiFi.channel(i), WiFi.RSSI(i),
			WiFi.encryptionType(i) == WIFI_AUTH_OPEN ? "OPEN" : "enc");
		if (WiFi.SSID(i) == String(ssid))
			seen = true;
	}
	Serial.printf("[scan] target \"%s\" %s\n", ssid,
		seen ? "FOUND (2.4GHz OK)" : "NOT in list -> wrong name or 5GHz-only hotspot");
	WiFi.scanDelete();
}

// Non-blocking WiFi: kick off / retry connect, never block loop().
void	serviceWifi(void)
{
	static wl_status_t	lastWs = (wl_status_t)255;
	wl_status_t			ws;

	ws = WiFi.status();
	if (ws != lastWs)
	{
		Serial.printf("[wifi] status=%s\n", wifiStatusStr(ws));
		lastWs = ws;
	}
	if (wifiUp())
		return ;
	// Issue WiFi.begin ONCE, then give the join (auth + DHCP, several seconds)
	// time to finish. Only retry after 15 s -- calling begin repeatedly restarts
	// the handshake and the connection never completes.
	if (lastWifiTry == 0 || millis() - lastWifiTry > 15000)
	{
		lastWifiTry = millis();
		Serial.print("[wifi] WiFi.begin -> ");
		Serial.println(ssid);
		WiFi.begin(ssid, password);
	}
}

// Non-blocking MQTT: one connect attempt every 3 s while WiFi is up.
void	serviceMqtt(void)
{
	String	clientId;

	if (!wifiUp() || mqttUp())
		return ;
	if (millis() - lastMqttTry < 3000)
		return ;
	lastMqttTry = millis();
	Serial.print("[mqtt] connecting... ");
	clientId = "ESP32_TopUp_" + String(random(0xffff), HEX);
	if (client.connect(clientId.c_str(), mqtt_user, mqtt_pass))
	{
		Serial.println("connected");
		client.subscribe("/check_role/response");
		client.subscribe("/add_money/response");
		client.subscribe("/create_guest/response");
		client.subscribe("/remove_card/response");
		client.subscribe("/check_balance/response");
		Serial.println("[mqtt] subscribed to response topics");
	}
	else
	{
		Serial.print("failed rc=");
		Serial.println(client.state());
	}
}

// LED: solid when fully connected, blink while connecting.
void	serviceLed(void)
{
	if (wifiUp() && mqttUp())
	{
		digitalWrite(LED_PIN, HIGH);
		return ;
	}
	if (millis() - lastBlink >= 250)
	{
		lastBlink = millis();
		ledState = !ledState;
		digitalWrite(LED_PIN, ledState);
	}
}

// Periodic one-line status log.
void	serviceStatus(void)
{
	if (millis() - lastStatus < 5000)
		return ;
	lastStatus = millis();
	Serial.print("[status] wifi=");
	Serial.print(wifiUp() ? "UP" : "DOWN");
	Serial.print(" mqtt=");
	Serial.print(mqttUp() ? "UP" : "DOWN");
	if (wifiUp())
	{
		Serial.print(" ip=");
		Serial.print(WiFi.localIP());
	}
	Serial.println();
}

// ---------- MQTT RX -> STM32 ----------

void	forwardResponse(const String &baseTopic, const String &payload)
{
	String	line;
	String	msg;

	line = "RESP " + baseTopic + " ";
	if (payload.indexOf("\"error\"") >= 0)
	{
		msg = jsonStr(payload, "error");
		msg.replace(' ', '_');
		if (msg.length() == 0)
			msg = "error";
		line += "ERR " + msg;
	}
	else if (baseTopic == TOPIC_CHECK_ROLE)
	{
		line += "OK " + jsonStr(payload, "role");
	}
	else if (baseTopic == TOPIC_ADD_MONEY)
	{
		line += "OK " + jsonNum(payload, "amount");
	}
	else if (baseTopic == TOPIC_CHECK_BALANCE)
	{
		line += "OK " + jsonNum(payload, "balance");
	}
	else
	{
		line += "OK";
	}
	Serial2.println(line);
	Serial.print("[-> STM32] ");
	Serial.println(line);
}

void	callback(char *topic, byte *payload, unsigned int length)
{
	String	msg;
	String	t;
	int		r;

	for (unsigned int i = 0; i < length; i++)
		msg += (char)payload[i];
	t = String(topic);
	Serial.print("\n[MQTT <-] ");
	Serial.print(t);
	Serial.print(" : ");
	Serial.println(msg);
	r = t.indexOf("/response");
	if (r >= 0)
		t = t.substring(0, r);
	forwardResponse(t, msg);
}

// ---------- STM32 CMD -> MQTT ----------

const char	*topicForAction(const String &action)
{
	if (action == "check_role")
		return (TOPIC_CHECK_ROLE);
	if (action == "add_money")
		return TOPIC_ADD_MONEY;
	if (action == "create_guest")
		return TOPIC_CREATE_GUEST;
	if (action == "remove_card")
		return TOPIC_REMOVE_CARD;
	if (action == "check_balance")
		return TOPIC_CHECK_BALANCE;
	return nullptr;
}

// Reply to the STM32's connection self-test.
void	handlePing(void)
{
	String	line;

	line = "RESP /ping ";
	if (!wifiUp())
		line += "ERR nowifi";
	else if (!mqttUp())
		line += "ERR nomqtt";
	else
		line += "OK wifi=1,mqtt=1";
	Serial2.println(line);
	Serial.print("[-> STM32] ");
	Serial.println(line);
}

void	handleCommand(const String &line)
{
	int			s1;
	int			s2;
	int			s3;
	String		action;
	String		uid;
	String		amount;
	const char	*topic;
	String		json;

	s1 = line.indexOf(' ');
	s2 = line.indexOf(' ', s1 + 1);
	s3 = line.indexOf(' ', s2 + 1);
	if (s1 < 0)
		return ;
	action = (s2 < 0) ? line.substring(s1 + 1) : line.substring(s1 + 1, s2);
	action.trim();
	if (action == "ping")
	{
		handlePing();
		return ;
	}
	if (s2 < 0)
		return ; // all other commands need a uid
	uid = (s3 < 0) ? line.substring(s2 + 1) : line.substring(s2 + 1, s3);
	amount = (s3 < 0) ? "" : line.substring(s3 + 1);
	uid.trim();
	amount.trim();
	topic = topicForAction(action);
	if (!topic)
	{
		Serial.print("[cmd] unknown action: ");
		Serial.println(action);
		return ;
	}
	// Can't reach the broker -> tell the STM32 now instead of making it wait.
	if (!mqttUp())
	{
		Serial2.println(String("RESP ") + topic + " ERR no_mqtt");
		Serial.println("[cmd] dropped, MQTT down");
		return ;
	}
	json = "{\"card_id\":\"" + uid + "\"";
	if (action == "add_money" && amount.length() > 0)
		json += ",\"amount\":" + amount;
	json += "}";
	if (client.publish(topic, json.c_str()))
	{
		Serial.print("[MQTT ->] ");
		Serial.print(topic);
		Serial.print(" : ");
		Serial.println(json);
	}
	else
	{
		Serial.println("[cmd] publish failed");
		Serial2.println(String("RESP ") + topic + " ERR publish_failed");
	}
}

void	serviceUart(void)
{
	String	in;

	if (Serial2.available() <= 0)
		return ;
	in = Serial2.readStringUntil('\n');
	in.trim();
	if (in.length() == 0)
		return ;
	if (in.startsWith("CMD "))
	{
		handleCommand(in);
	}
	else
	{
		Serial.print("[STM32 log] ");
		Serial.println(in);
	}
}

// ---------- setup / loop ----------

void	setup(void)
{
	Serial.begin(115200);
	Serial2.begin(115200, SERIAL_8N1, RXD2, TXD2);
	pinMode(LED_PIN, OUTPUT);
	digitalWrite(LED_PIN, LOW);
	espClient.setInsecure(); // skip TLS cert check (port 8883)
	client.setServer(mqtt_server, mqtt_port);
	client.setCallback(callback);
	// Force station mode + clean any stale config, then show what we can see.
	WiFi.mode(WIFI_STA);
	WiFi.setSleep(false);
	WiFi.disconnect(true);
	delay(100);
	Serial.printf("\n[cfg] target SSID=\"%s\" passlen=%d\n", ssid,
		(int)strlen(password));
	scanWifi();
	Serial.println("ESP32 top-up bridge starting. Connecting WiFi/MQTT...");
}

void	loop(void)
{
	serviceWifi();
	serviceMqtt();
	if (mqttUp())
		client.loop();
	serviceLed();
	serviceStatus();
	serviceUart();
}
