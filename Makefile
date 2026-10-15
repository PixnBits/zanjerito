# Static binary. On the Pi, uname -m picks the arch; from a laptop set GOARCH=.
GO       ?= go
CGO      ?= 0
BIN      ?= zanjerito
UNAME_M  := $(shell uname -m)
ifeq ($(GOARCH),)
  ifeq ($(UNAME_M),x86_64)
    GOARCH := amd64
  else ifeq ($(UNAME_M),aarch64)
    GOARCH := arm64
  else ifeq ($(UNAME_M),arm64)
    GOARCH := arm64
  else ifneq ($(filter armv7l armv6l arm,$(UNAME_M)),)
    GOARCH := arm
    GOARM  ?= 7
  else
    GOARCH := amd64
  endif
endif

.PHONY: build test install build-mqtt build-mqtt-arm build-mqtt-arm64

build:
	CGO_ENABLED=$(CGO) GOOS=linux GOARCH=$(GOARCH) $(if $(GOARM),GOARM=$(GOARM),) $(GO) build -trimpath -buildvcs=false -ldflags='-s -w' -o $(BIN) ./cmd/zanjerito

test:
	$(GO) test ./... -race

install: build
	./deploy/install.sh

build-mqtt:
	CGO_ENABLED=$(CGO) GOOS=linux GOARCH=$(GOARCH) $(if $(GOARM),GOARM=$(GOARM),) $(GO) build -trimpath -buildvcs=false -ldflags='-s -w' -o zanjerito-mqtt ./cmd/zanjerito-mqtt

build-mqtt-arm:
	mkdir -p dist
	CGO_ENABLED=0 GOOS=linux GOARCH=arm GOARM=7 $(GO) build -trimpath -buildvcs=false -ldflags='-s -w' -o dist/zanjerito-mqtt-linux-armv7 ./cmd/zanjerito-mqtt

build-mqtt-arm64:
	mkdir -p dist
	CGO_ENABLED=0 GOOS=linux GOARCH=arm64 $(GO) build -trimpath -buildvcs=false -ldflags='-s -w' -o dist/zanjerito-mqtt-linux-arm64 ./cmd/zanjerito-mqtt
