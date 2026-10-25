package hamqtt

import "errors"

// Publisher is the broker client. internal/hamqtt does not import a broker SDK;
// cmd/zanjerito-mqtt supplies a paho adapter.
type Publisher interface {
	Publish(topic string, qos byte, retained bool, payload []byte) error
	// Connected is true only when a publish would hit an open session.
	// Callers skip Publish while this is false so nothing is queued.
	Connected() bool
}

// ErrNotConnected means the broker session is not open. Publish must return
// this without queueing the payload.
var ErrNotConnected = errors.New("mqtt: not connected")

// Fixed MQTT topics. Discovery uses Home Assistant device discovery.
const (
	TopicDiscovery    = "homeassistant/device/zanjerito/config"
	TopicState        = "zanjerito/state"
	TopicAvailability = "zanjerito/availability"
	TopicHAStatus     = "homeassistant/status"

	PayloadOnline  = "online"
	PayloadOffline = "offline"

	uniqueIDPrefix = "zanjerito_mqtt_"
)
