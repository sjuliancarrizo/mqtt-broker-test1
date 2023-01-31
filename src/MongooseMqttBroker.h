#ifndef MongooseMqttBroker_h
#define MongooseMqttBroker_h

#ifdef ARDUINO
#include "Arduino.h"
#endif

#include "mongoose.h"

// Linked list management macros
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

uint16_t mg_ntohs(uint16_t net);

#define mg_htons(x) mg_ntohs(x)

struct sub 
{
  struct sub *next;
  struct mg_connection *c;
  struct mg_str topic;
  uint8_t qos;
};

static struct sub *_subs;

class MongooseMqttBroker
{
  private:
    const char *_listen_on;
    bool _running;
    //A list of subscription
    
    struct mg_connection *_c;

    static void defaultEventHandler(struct mg_connection *c, int ev, void *ev_data, void *u);
    void eventHandler(struct mg_connection *c, int ev, void *ev_data);

    bool mg_match(struct mg_str s, struct mg_str p, struct mg_str *caps);
    static size_t mg_mqtt_next_topic(struct mg_mqtt_message *msg, struct mg_str *topic, uint8_t *qos, size_t pos);
    size_t mg_mqtt_next_sub(struct mg_mqtt_message *msg, struct mg_str *topic, uint8_t *qos, size_t pos);
    size_t mg_mqtt_next_unsub(struct mg_mqtt_message *msg, struct mg_str *topic, size_t pos);
    void mg_mqtt_send_header(struct mg_connection *c, uint8_t cmd, uint8_t flags, uint32_t len);


  public:
    MongooseMqttBroker();
    ~MongooseMqttBroker();

    bool begin(mg_mgr *mgr, const char *address);

    //For stop and close the broker
    bool close();
};



#endif /* MongooseMqttBroker_h */