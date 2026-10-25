package main

import (
	"context"
	"flag"
	"fmt"
	"log"
	"math/rand"
	"os"
	"os/signal"
	"syscall"
	"time"

	mqtt "github.com/eclipse/paho.mqtt.golang"

	"github.com/PixnBits/zanjerito/internal/hamqtt"
)

// version is settable via -ldflags "-X main.version=...".
var version = "dev"

// opTimeout bounds one publish or subscribe wait so a stuck broker cannot
// hold shutdown past TimeoutStopSec.
const opTimeout = 2 * time.Second

type pahoPublisher struct {
	c mqtt.Client
}

func (p *pahoPublisher) Connected() bool {
	return p != nil && p.c != nil && p.c.IsConnectionOpen()
}

func (p *pahoPublisher) Publish(topic string, qos byte, retained bool, payload []byte) error {
	// Check before Publish. While AutoReconnect is set, paho still accepts
	// QoS 1 publishes during reconnect and replays them from its store.
	if !p.Connected() {
		return hamqtt.ErrNotConnected
	}
	tok := p.c.Publish(topic, qos, retained, payload)
	if !tok.WaitTimeout(opTimeout) {
		return fmt.Errorf("mqtt publish %s: timeout", topic)
	}
	if err := tok.Error(); err != nil {
		return fmt.Errorf("mqtt publish %s: %w", topic, err)
	}
	return nil
}

func main() {
	showVersion := flag.Bool("version", false, "print version and exit")
	flag.Parse()
	if *showVersion {
		fmt.Println(version)
		return
	}

	cfg, err := hamqtt.LoadConfig(os.Getenv)
	if err != nil {
		log.Fatalf("config: %v", err)
	}

	log.Print(hamqtt.StartupLine(cfg, version))

	fetch := hamqtt.NewHTTPFetcher(cfg.HAURL, cfg.PollInterval)
	bridge := &hamqtt.Bridge{
		Version: version,
		Fetch:   fetch,
		Refresh: cfg.RefreshInterval,
		Delay: func() time.Duration {
			return time.Duration(rand.Int63n(int64(2 * time.Second)))
		},
		Logf: func(format string, args ...any) {
			log.Print(redactText(cfg, fmt.Sprintf(format, args...)))
		},
	}

	opts := mqtt.NewClientOptions()
	opts.AddBroker(cfg.URL)
	opts.SetClientID(cfg.ClientID)
	if cfg.User != "" {
		opts.SetUsername(cfg.User)
	}
	if cfg.Pass != "" {
		opts.SetPassword(cfg.Pass)
	}
	opts.SetKeepAlive(15 * time.Second)
	opts.SetCleanSession(true)
	opts.SetResumeSubs(false)
	opts.SetStore(mqtt.NewMemoryStore())
	opts.SetAutoReconnect(true)
	opts.SetConnectRetry(true)
	opts.SetConnectRetryInterval(5 * time.Second)
	opts.SetMaxReconnectInterval(time.Minute)
	opts.SetWill(hamqtt.TopicAvailability, hamqtt.PayloadOffline, 1, true)
	opts.SetConnectionLostHandler(func(_ mqtt.Client, err error) {
		if err == nil {
			log.Printf("mqtt disconnected")
			return
		}
		log.Printf("mqtt disconnected: %s", redactText(cfg, err.Error()))
	})
	opts.SetOnConnectHandler(func(c mqtt.Client) {
		tok := c.Subscribe(hamqtt.TopicHAStatus, 1, func(_ mqtt.Client, msg mqtt.Message) {
			payload := append([]byte(nil), msg.Payload()...)
			go bridge.OnHAStatus(payload)
		})
		if !tok.WaitTimeout(opTimeout) {
			log.Printf("subscribe %s: timeout", hamqtt.TopicHAStatus)
		} else if err := tok.Error(); err != nil {
			log.Printf("subscribe %s: %s", hamqtt.TopicHAStatus, redactText(cfg, err.Error()))
		}
		bridge.OnConnect()
	})

	client := mqtt.NewClient(opts)
	pub := &pahoPublisher{c: client}
	bridge.Pub = loggingPublisher{
		inner: pub,
		redact: func(s string) string {
			return redactText(cfg, s)
		},
	}

	tok := client.Connect()
	go func() {
		if !tok.Wait() {
			log.Printf("mqtt connect: client closed")
			return
		}
		if err := tok.Error(); err != nil {
			log.Printf("mqtt connect: %s", redactText(cfg, err.Error()))
		}
	}()

	ctx, stop := signal.NotifyContext(context.Background(), os.Interrupt, syscall.SIGTERM)
	defer stop()

	bridge.Poll(ctx)
	tick := time.NewTicker(cfg.PollInterval)
	defer tick.Stop()
	for {
		select {
		case <-ctx.Done():
			bridge.Shutdown()
			client.Disconnect(250)
			log.Printf("zanjerito-mqtt stopped")
			return
		case <-tick.C:
			bridge.Poll(ctx)
		}
	}
}

func redactText(cfg hamqtt.Config, s string) string {
	secrets := append([]string{cfg.Pass}, hamqtt.URLPasswords(cfg.URL)...)
	return hamqtt.Redact(s, secrets...)
}

type loggingPublisher struct {
	inner  hamqtt.Publisher
	redact func(string) string
}

func (p loggingPublisher) Connected() bool {
	if p.inner == nil {
		return false
	}
	return p.inner.Connected()
}

func (p loggingPublisher) Publish(topic string, qos byte, retained bool, payload []byte) error {
	err := p.inner.Publish(topic, qos, retained, payload)
	if err != nil {
		msg := err.Error()
		if p.redact != nil {
			msg = p.redact(msg)
		}
		log.Printf("mqtt publish %s: %s", topic, msg)
	}
	return err
}
