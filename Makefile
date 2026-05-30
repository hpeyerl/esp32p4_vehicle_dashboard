ENV = stub_debug
FIRMWARE = .pio/build/$(ENV)/firmware.bin
OTA_HOST = ev-dashboard.local

all: build

build:
	pio run -e $(ENV)

ota:
	curl -X POST http://$(OTA_HOST)/update \
      -H "Content-Type: application/octet-stream" \
      -H "Expect:" \
      --data-binary @$(FIRMWARE) \
      --progress-bar \
      --max-time 120

deploy: build ota

.PHONY: all build ota deploy
