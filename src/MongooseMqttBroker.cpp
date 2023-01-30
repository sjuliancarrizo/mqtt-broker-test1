#ifdef ARDUINO
#include <Arduino.h>
#endif

// #include "MongooseCore.h"
#include "MongooseMqttBroker.h"

// static struct sub *_subs = NULL;

MongooseMqttBroker::MongooseMqttBroker() : _running(false),
                                           _subs(NULL),
                                           _c(NULL),
                                           _listen_on(NULL)
{
}

MongooseMqttBroker::~MongooseMqttBroker()
{
}

bool MongooseMqttBroker::begin(mg_mgr *mgr, const char *address)
{
  if ((_c = mg_bind(mgr, address, (mg_event_handler_t)eventHandler, NULL)) == NULL)
  {
    Serial.println("mg_bind failed");
    return false;
  }

  mg_set_protocol_mqtt(_c);
  _running = true;

  return true;
}

void MongooseMqttBroker::eventHandler(struct mg_connection *c, int ev, void *ev_data, void *u)
{
  MongooseMqttBroker *self = (MongooseMqttBroker *)u;
  self->eventHandler(c,ev,ev_data);
}

void MongooseMqttBroker::eventHandler(struct mg_connection *c, int ev, void *ev_data)
{
  if (ev != MG_EV_POLL)
  {
    printf("USER HANDLER GOT EVENT %d\n", ev);
    // If is grater than 200, the it's a mqtt command.
    if (ev > 200)
    {
      struct mg_mqtt_message *mm = (struct mg_mqtt_message *)ev_data;
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
          struct sub *sub = (struct sub *)calloc(1, sizeof(*sub));
          sub->c = c;
          sub->topic = mg_strdup(topic);
          sub->qos = qos;
          LIST_ADD_HEAD(struct sub, &_subs, sub);

          // Change '+' to '*' for topic matching using mg_match
          for (size_t i = 0; i < sub->topic.len; i++)
          {
            if (sub->topic.p[i] == '+')
              ((char *)sub->topic.p)[i] = '*';
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
        for (struct sub *sub = _subs; sub != NULL; sub = sub->next)
        {
          if (mg_match(mm->topic, sub->topic, NULL))
          {
            char buf[100], *p = buf;
            mg_asprintf(&p, sizeof(buf), "%.*s", (int)mm->topic.len, mm->topic.p);
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
      // Should I do something here?
    }
    else if (ev == MG_EV_CLOSE)
    {
      // Client disconnects. Remove from the subscription list
      for (struct sub *next, *sub = _subs; sub != NULL; sub = next)
      {
        next = sub->next;
        if (c != sub->c)
          continue;
        LIST_DELETE(struct sub, &_subs, sub);
      }
    }
  }
}

bool MongooseMqttBroker::mg_match(struct mg_str s, struct mg_str p, struct mg_str *caps)
{
  size_t i = 0, j = 0, ni = 0, nj = 0;
  if (caps)
    caps->p = NULL, caps->len = 0;
  while (i < p.len || j < s.len)
  {
    if (i < p.len && j < s.len && (p.p[i] == '?' || s.p[j] == p.p[i]))
    {
      if (caps == NULL)
      {
      }
      else if (p.p[i] == '?')
      {
        caps->p = &s.p[j], caps->len = 1;      // Finalize `?` cap
        caps++, caps->p = NULL, caps->len = 0; // Init next cap
      }
      else if (caps->p != NULL && caps->len == 0)
      {
        caps->len = (size_t)(&s.p[j] - caps->p); // Finalize current cap
        caps++, caps->len = 0, caps->p = NULL;   // Init next cap
      }
      i++, j++;
    }
    else if (i < p.len && (p.p[i] == '*' || p.p[i] == '#'))
    {
      if (caps && !caps->p)
        caps->len = 0, caps->p = &s.p[j]; // Init cap
      ni = i++, nj = j + 1;
    }
    else if (nj > 0 && nj <= s.len && (p.p[ni] == '#' || s.p[j] != '/'))
    {
      i = ni, j = nj;
      if (caps && caps->p == NULL && caps->len == 0)
      {
        caps--, caps->len = 0; // Restart previous cap
      }
    }
    else
    {
      return false;
    }
  }
  if (caps && caps->p && caps->len == 0)
  {
    caps->len = (size_t)(&s.p[j] - caps->p);
  }
  return true;
}



size_t MongooseMqttBroker::mg_mqtt_next_sub(struct mg_mqtt_message *msg, struct mg_str *topic, uint8_t *qos, size_t pos)
{
  uint8_t tmp;
  return mg_mqtt_next_topic(msg, topic, qos == NULL ? &tmp : qos, pos);
}

size_t MongooseMqttBroker::mg_mqtt_next_unsub(struct mg_mqtt_message *msg, struct mg_str *topic, size_t pos)
{
  return mg_mqtt_next_topic(msg, topic, NULL, pos);
}

void MongooseMqttBroker::mg_mqtt_send_header(struct mg_connection *c, uint8_t cmd, uint8_t flags, uint32_t len)
{
  uint8_t buf[1 + sizeof(len)], *vlen = &buf[1];
  buf[0] = (uint8_t)((cmd << 4) | flags);
  do
  {
    *vlen = len % 0x80;
    len /= 0x80;
    if (len > 0)
      *vlen |= 0x80;
    vlen++;
  } while (len > 0 && vlen < &buf[sizeof(buf)]);
  mg_send(c, buf, (size_t)(vlen - buf));
}



bool MongooseMqttBroker::close()
{
  return true;
}

uint16_t mg_ntohs(uint16_t net)
{
  uint8_t data[2] = {0, 0};
  memcpy(&data, &net, sizeof(data));
  return (uint16_t)((uint16_t)data[1] | (((uint16_t)data[0]) << 8));
}

size_t MongooseMqttBroker::mg_mqtt_next_topic(struct mg_mqtt_message *msg, struct mg_str *topic, uint8_t *qos, size_t pos)
{
  unsigned char *buf = (unsigned char *)msg->payload.p + pos;
  size_t new_pos;

  if ((size_t)pos >= msg->payload.len)
    return -1;

  topic->len = buf[0] << 8 | buf[1];
  topic->p = (char *)buf + 2;
  new_pos = pos + 2 + topic->len + 1;
  if ((size_t)new_pos > msg->payload.len)
    return -1;
  *qos = buf[2 + topic->len];
  return new_pos;
}

