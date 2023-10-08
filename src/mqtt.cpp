/*
  project: nut2mqtt
  file:    mqtt.cpp
  author:  raymond@burkholder.net
  date:    2023/10/01 15:58:25
*/

#include <iostream>

#include "mqtt.hpp"

#define QOS         1
#define TIMEOUT     250L

Mqtt::Mqtt( const config::Values& choices, boost::asio::io_context& io_context, const char* szHostName )
: m_bCreated( false ), m_bConnected( false )
, m_io_context( io_context )
, m_conn_opts( MQTTClient_connectOptions_initializer )
, m_pubmsg( MQTTClient_message_initializer )
, m_sMqttUrl( "tcp://" + choices.mqtt.sHost + ":1883" )
{
  m_conn_opts.keepAliveInterval = 20;
  m_conn_opts.cleansession = 1;
  m_conn_opts.username = choices.mqtt.sUserName.c_str();
  m_conn_opts.password = choices.mqtt.sPassword.c_str();

  int rc;

  rc = MQTTClient_create(
    &m_clientMqtt, m_sMqttUrl.c_str(), szHostName, // might use choices.mqtt.id
    MQTTCLIENT_PERSISTENCE_NONE, nullptr
    );

  if ( MQTTCLIENT_SUCCESS != rc ) {
    throw( runtime_error( "Failed to create client", rc ) );
  }

  m_bCreated = true;

  rc = MQTTClient_setCallbacks( m_clientMqtt, this, &Mqtt::connectionLost, &Mqtt::messageArrived, &Mqtt::deliveryComplete );

  rc = MQTTClient_connect( m_clientMqtt, &m_conn_opts );

  if ( MQTTCLIENT_SUCCESS != rc ) {
    throw( runtime_error( "Failed to connect", rc ) );
  }

  m_bConnected = true;
}

Mqtt::~Mqtt() {
  if ( m_bCreated ) {
    if ( m_bConnected ) {
      int rc = MQTTClient_disconnect( m_clientMqtt, 10000 );
      if ( MQTTCLIENT_SUCCESS != rc ) {
        std::cerr << "Failed to disconnect, return code " << rc << std::endl;
      }
    }
    MQTTClient_destroy( &m_clientMqtt );
  }
}

void Mqtt::Publish( const std::string& sTopic, const std::string& sMessage, fPublishComplete_t&& fPublishComplete ) {
  if ( m_bConnected ) {
    m_pubmsg.payload = (void*) sMessage.begin().base();
    m_pubmsg.payloadlen = sMessage.size();
    m_pubmsg.qos = QOS;
    m_pubmsg.retained = 0;
    MQTTClient_deliveryToken token;
    int rc = MQTTClient_publishMessage( m_clientMqtt, sTopic.c_str(), &m_pubmsg, &token );
    if ( MQTTCLIENT_SUCCESS != rc ) {
      fPublishComplete( false, rc );
      //throw( runtime_error( "Failed to publish message", rc ) );
    }
    else {
      std::lock_guard<std::mutex> lock( m_mutexDeliveryToken );
      umapDeliveryToken_t::iterator iterDeliveryToken = m_umapDeliveryToken.find( token );
      if ( m_umapDeliveryToken.end() == iterDeliveryToken ) {
        m_umapDeliveryToken.emplace( token, std::move( fPublishComplete ) );
      }
      else {
        std::cerr << "delivery token " << token << " already delivered" << std::endl;
        fPublishComplete( true, 0 );
        assert( nullptr == iterDeliveryToken->second );
        m_umapDeliveryToken.erase( iterDeliveryToken );
      }
    }
  }
}

void Mqtt::connectionLost(void* context, char* cause) {
  Mqtt* self = reinterpret_cast<Mqtt*>( context );
  std::cerr << "mqtt connection lost" << std::endl;
}

int Mqtt::messageArrived(void* context, char* topicName, int topicLen, MQTTClient_message* message) {
  Mqtt* self = reinterpret_cast<Mqtt*>( context );
  std::cout << "mqtt message: " << std::string( topicName, topicLen ) << " " << std::string( (const char*) message->payload, message->payloadlen ) << std::endl;
  MQTTClient_freeMessage( &message );
  MQTTClient_free( topicName );
  return 1;
}

void Mqtt::deliveryComplete( void* context, MQTTClient_deliveryToken token ) {
  Mqtt* self = reinterpret_cast<Mqtt*>( context );
  //std::cout << "mqtt delivery complete" << std::endl;
  std::lock_guard<std::mutex> lock( self->m_mutexDeliveryToken );
  umapDeliveryToken_t::iterator iterDeliveryToken = self->m_umapDeliveryToken.find( token );
  if ( self->m_umapDeliveryToken.end() == iterDeliveryToken ) {
    std::cerr << "delivery token " << token << " not yet registered" << std::endl;
    self->m_umapDeliveryToken.emplace( token, nullptr );
  }
  else {
    iterDeliveryToken->second( true, 0 );
    self->m_umapDeliveryToken.erase( iterDeliveryToken );
  }
}
