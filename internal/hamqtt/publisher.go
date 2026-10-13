package hamqtt

// Publisher is the broker client. internal/hamqtt does not import a broker SDK;
// cmd/zanjerito-mqtt supplies a paho adapter.
type Publisher interface {
	Publish(topic string, qos byte, retained bool, payload []byte) error
}

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
