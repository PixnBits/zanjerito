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

const publishWait = 5 * time.Second

type pahoPublisher struct {
	c mqtt.Client
}

func (p *pahoPublisher) Publish(topic string, qos byte, retained bool, payload []byte) error {
	if p == nil || p.c == nil {
		return fmt.Errorf("mqtt: no client")
	}
	tok := p.c.Publish(topic, qos, retained, payload)
	if !tok.WaitTimeout(publishWait) {
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

	user := "(none)"
	if cfg.User != "" {
		user = cfg.User
	}
	log.Printf("zanjerito-mqtt %s broker=%s client_id=%s user=%s ha=%s poll=%s refresh=%s",
		version, cfg.URL, cfg.ClientID, user, cfg.HAURL, cfg.PollInterval, cfg.RefreshInterval)

	fetch := hamqtt.NewHTTPFetcher(cfg.HAURL, cfg.PollInterval)
	var lastFetchErr string
	bridge := &hamqtt.Bridge{
		Version: version,
		Fetch: func(ctx context.Context) ([]byte, error) {
			// Log transitions only, so a stopped daemon does not flood the journal.
			body, err := fetch(ctx)
			switch {
			case err != nil && err.Error() != lastFetchErr:
				log.Printf("ha fetch: %v", err)
				lastFetchErr = err.Error()
			case err == nil && lastFetchErr != "":
				log.Printf("ha fetch: recovered")
				lastFetchErr = ""
			}
			return body, err
		},
		Refresh: cfg.RefreshInterval,
		Delay: func() time.Duration {
			return time.Duration(rand.Int63n(int64(2 * time.Second)))
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
	opts.SetAutoReconnect(true)
	opts.SetConnectRetry(true)
	opts.SetConnectRetryInterval(5 * time.Second)
	opts.SetMaxReconnectInterval(time.Minute)
	opts.SetWill(hamqtt.TopicAvailability, hamqtt.PayloadOffline, 1, true)
	opts.SetConnectionLostHandler(func(_ mqtt.Client, err error) {
		log.Printf("mqtt disconnected: %v", err)
	})
	opts.SetOnConnectHandler(func(c mqtt.Client) {
		tok := c.Subscribe(hamqtt.TopicHAStatus, 1, func(_ mqtt.Client, msg mqtt.Message) {
			payload := append([]byte(nil), msg.Payload()...)
			go bridge.OnHAStatus(payload)
		})
		if !tok.WaitTimeout(publishWait) {
			log.Printf("subscribe %s: timeout", hamqtt.TopicHAStatus)
		} else if err := tok.Error(); err != nil {
			log.Printf("subscribe %s: %v", hamqtt.TopicHAStatus, err)
		}
		bridge.OnConnect()
	})

	client := mqtt.NewClient(opts)
	pub := &pahoPublisher{c: client}
	bridge.Pub = loggingPublisher{inner: pub}

	tok := client.Connect()
	go func() {
		if !tok.Wait() {
			log.Printf("mqtt connect: client closed")
			return
		}
		if err := tok.Error(); err != nil {
			log.Printf("mqtt connect: %v", err)
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

type loggingPublisher struct {
	inner hamqtt.Publisher
}

func (p loggingPublisher) Publish(topic string, qos byte, retained bool, payload []byte) error {
	err := p.inner.Publish(topic, qos, retained, payload)
	if err != nil {
		log.Printf("mqtt publish %s: %v", topic, err)
	}
	return err
}
