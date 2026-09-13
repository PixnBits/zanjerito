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

.PHONY: build test install

build:
	CGO_ENABLED=$(CGO) GOOS=linux GOARCH=$(GOARCH) $(if $(GOARM),GOARM=$(GOARM),) $(GO) build -trimpath -ldflags='-s -w' -o $(BIN) ./cmd/zanjerito

test:
	$(GO) test ./... -race

install: build
	./deploy/install.sh
