#include <Arduino.h>
#include "WiFi.h"
#include "mongoose.h"

//---------------
// Things to put in a separated file

#define LIST_ADD_HEAD(type_, head_, elem_) \
  do {                                     \
    (elem_)->next = (*head_);              \
    *(head_) = (elem_);                    \
  } while (0)

#define LIST_ADD_TAIL(type_, head_, elem_) \
  do {                                     \
    type_ **h = head_;                     \
    while (*h != NULL) h = &(*h)->next;    \
    *h = (elem_);                          \
  } while (0)

#define LIST_DELETE(type_, head_, elem_)   \
  do {                                     \
    type_ **h = head_;                     \
    while (*h != (elem_)) h = &(*h)->next; \
    *h = (elem_)->next;                    \
  } while (0)

uint16_t mg_ntohs(uint16_t net) {
  uint8_t data[2] = {0, 0};
  memcpy(&data, &net, sizeof(data));
  return (uint16_t) ((uint16_t) data[1] | (((uint16_t) data[0]) << 8));
}

#define mg_htons(x) mg_ntohs(x)

bool mg_match(struct mg_str s, struct mg_str p, struct mg_str *caps) {
  size_t i = 0, j = 0, ni = 0, nj = 0;
  if (caps) caps->p = NULL, caps->len = 0;
  while (i < p.len || j < s.len) {
    if (i < p.len && j < s.len && (p.p[i] == '?' || s.p[j] == p.p[i])) {
      if (caps == NULL) {
      } else if (p.p[i] == '?') {
        caps->p = &s.p[j], caps->len = 1;     // Finalize `?` cap
        caps++, caps->p = NULL, caps->len = 0;  // Init next cap
      } else if (caps->p != NULL && caps->len == 0) {
        caps->len = (size_t) (&s.p[j] - caps->p);  // Finalize current cap
        caps++, caps->len = 0, caps->p = NULL;       // Init next cap
      }
      i++, j++;
    } else if (i < p.len && (p.p[i] == '*' || p.p[i] == '#')) {
      if (caps && !caps->p) caps->len = 0, caps->p = &s.p[j];  // Init cap
      ni = i++, nj = j + 1;
    } else if (nj > 0 && nj <= s.len && (p.p[ni] == '#' || s.p[j] != '/')) {
      i = ni, j = nj;
      if (caps && caps->p == NULL && caps->len == 0) {
        caps--, caps->len = 0;  // Restart previous cap
      }
    } else {
      return false;
    }
  }
  if (caps && caps->p && caps->len == 0) {
    caps->len = (size_t) (&s.p[j] - caps->p);
  }
  return true;
}

//-----------------

const char * ssid = "South Africa 2.4G";
const char * password = "redcasa88";

//static const char *s_listen_on = "mqtt://0.0.0.0:1883";
static const char *s_listen_on = "0.0.0.0:1883";

// A list of subscription, held in memory
struct sub {
  struct sub *next;
  struct mg_connection *c;
  struct mg_str topic;
  uint8_t qos;
};
static struct sub *s_subs = NULL;

static size_t mg_mqtt_next_topic(struct mg_mqtt_message *msg,
                                 struct mg_str *topic, uint8_t *qos,
                                 size_t pos) {
  unsigned char *buf = (unsigned char *) msg->payload.p + pos;
  size_t new_pos;

  if ((size_t) pos >= msg->payload.len) return -1;

  topic->len = buf[0] << 8 | buf[1];
  topic->p = (char *) buf + 2;
  new_pos = pos + 2 + topic->len + 1;
  if ((size_t) new_pos > msg->payload.len) return -1;
  *qos = buf[2 + topic->len];
  return new_pos;
}

size_t mg_mqtt_next_sub(struct mg_mqtt_message *msg, struct mg_str *topic,
                        uint8_t *qos, size_t pos) {
  uint8_t tmp;
  return mg_mqtt_next_topic(msg, topic, qos == NULL ? &tmp : qos, pos);
}

size_t mg_mqtt_next_unsub(struct mg_mqtt_message *msg, struct mg_str *topic,
                          size_t pos) {
  return mg_mqtt_next_topic(msg, topic, NULL, pos);
}

void mg_mqtt_send_header(struct mg_connection *c, uint8_t cmd, uint8_t flags,
                         uint32_t len) {
  uint8_t buf[1 + sizeof(len)], *vlen = &buf[1];
  buf[0] = (uint8_t) ((cmd << 4) | flags);
  do {
    *vlen = len % 0x80;
    len /= 0x80;
    if (len > 0) *vlen |= 0x80;
    vlen++;
  } while (len > 0 && vlen < &buf[sizeof(buf)]);
  mg_send(c, buf, (size_t) (vlen - buf));
}
              
static void ev_handler(struct mg_connection *c, int ev, void *ev_data) 
{
  if (ev != MG_EV_POLL) 
  {
    printf("USER HANDLER GOT EVENT %d\n", ev);
    //If is grater than 200, the it's a mqtt command.
    if (ev > 200)
    {
      struct mg_mqtt_message *mm = (struct mg_mqtt_message *) ev_data;
      Serial.print("cmd ");
      Serial.println(mm->cmd);

      switch (ev)
      {
        case MG_EV_MQTT_CONNECT:
        {
          // Client connects
          if (mm->len < 9) 
          {
            Serial.println("Malformed MQTT frame");
          } 
          else if (mm->protocol_version != 4) 
          {
            Serial.println("Unsupported MQTT version ");
            Serial.print(mm->protocol_version);
          }
          else 
          {
            uint8_t response[] = {0, 0};
            mg_mqtt_send_header(c, MG_MQTT_CMD_CONNACK, 0, sizeof(response));
            mg_send(c, response, sizeof(response));
          }
        break;
        }
        case MG_EV_MQTT_SUBSCRIBE:
        {
          // Client subscribes
          size_t pos = 0;  
          uint8_t qos, resp[256];
          struct mg_str topic;
          int num_topics = 0;
          while ((pos = mg_mqtt_next_sub(mm, &topic, &qos, pos)) != -1) 
          {
            struct sub *sub = (struct sub*) calloc(1, sizeof(*sub));
            sub->c = c;
            sub->topic = mg_strdup(topic);
            sub->qos = qos;
            LIST_ADD_HEAD(struct sub, &s_subs, sub);

            // Change '+' to '*' for topic matching using mg_match
            for (size_t i = 0; i < sub->topic.len; i++) 
            {
              if (sub->topic.p[i] == '+') ((char *) sub->topic.p)[i] = '*';
            }
            resp[num_topics++] = qos;
          }
          mg_mqtt_send_header(c, MG_MQTT_CMD_SUBACK, 0, num_topics + 2);
          uint16_t id = mg_htons(mm->message_id);
          mg_send(c, &id, 2);
          mg_send(c, resp, num_topics);

        break;
        }
        case MG_EV_MQTT_PUBLISH:
        {
          for (struct sub *sub = s_subs; sub != NULL; sub = sub->next) 
          {
            if (mg_match(mm->topic, sub->topic, NULL)) 
            {
              char buf[100], *p = buf;
              mg_asprintf(&p, sizeof(buf), "%.*s", (int) mm->topic.len, mm->topic.p);
              if (p == NULL) 
              {
                return;
              }                
              mg_mqtt_publish(sub->c, p, mm->message_id, 0, mm->payload.p, mm->payload.len);
              if (p != buf) 
              {
                free(p);
              }
            }
          }
        break;

        }
        case MG_EV_MQTT_PINGREQ: 
        {
        // The server must send a PINGRESP packet in response to a PINGREQ packet [MQTT-3.12.4-1]
        mg_mqtt_send_header(c, MG_MQTT_CMD_PINGRESP, 0, 0);
        break;
        }

      }
    }
    else if (ev == MG_EV_ACCEPT)
    {
      //Should I do something here?
    }
    else if (ev == MG_EV_CLOSE)
    {
      // Client disconnects. Remove from the subscription list
      for (struct sub *next, *sub = s_subs; sub != NULL; sub = next) 
      {
      next = sub->next;
      if (c != sub->c) continue;
      LIST_DELETE(struct sub, &s_subs, sub);
      }
    }
  
  }

}

void brokerv7_adapt()
{
  struct mg_mgr mgr;
  struct mg_connection *c;
  mg_mgr_init(&mgr,NULL);

  if ((c = mg_bind(&mgr, s_listen_on, (mg_event_handler_t) ev_handler, NULL)) == NULL) 
  {
    Serial.println("mg_bind failed");
    return;
  }

  Serial.println("Bind ok.");

  //Attaches a built-in MQTT event handler to the given connection.
  mg_set_protocol_mqtt(c);

  Serial.println("Polling.");

  for (;;) {
    mg_mgr_poll(&mgr, 1000);
  }

}

/***********************************************
 *  Funciones para manejor de conectividad WiFi
 ***********************************************/
int connectWifi()
{
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);
  int result = WiFi.waitForConnectResult();
  switch (result)
  {
  case WL_CONNECTED:
    Serial.println("\nWiFi connected.");
    Serial.println(WiFi.localIP());
    return 0;

  case WL_CONNECT_FAILED:
    Serial.println("\nWiFi connect failed.");
    return -1;

  default:
    Serial.println("\nOther WiFi error: ");
    Serial.print(result);
    return -1;
  }
}



void setup() {
  delay(3000);
  Serial.begin(115200);
  if (connectWifi() != 0)
    Serial.println("Wifi error.");

  brokerv7_adapt();

}

void loop() {
  Serial.println("looping");
  delay(2000);
}