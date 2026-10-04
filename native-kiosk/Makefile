# Core sources (reused by later UI / cross-compile targets).
SRC_CORE = src/zk_model.c src/zk_logic.c src/zk_layout.c src/zk_http.c src/zk_actions.c src/zk_text.c src/zk_console.c src/zk_power.c
SRC_DATA = src/data.c
CJSON = third_party/cJSON/cJSON.c

CC ?= gcc
CFLAGS = -std=gnu11 -Wall -Wextra -Werror -Os
INCLUDES = -Isrc -Ithird_party/cJSON -Itests
BUILD = build

SANITIZE := $(shell printf 'int main(void){return 0;}\n' | $(CC) -fsanitize=address,undefined -x c -o /dev/null - >/dev/null 2>&1 && echo -fsanitize=address,undefined)

TEST_BINS = $(BUILD)/test_json $(BUILD)/test_logic $(BUILD)/test_layout $(BUILD)/test_http $(BUILD)/test_data $(BUILD)/test_stop_latency $(BUILD)/test_text $(BUILD)/test_ui_text $(BUILD)/test_power

HOST_CC := gcc
ARM_CC ?= arm-linux-gnueabihf-gcc
LVGL_DIR := .cache/lvgl

APP_SRCS := $(SRC_CORE) $(SRC_DATA) src/main.c src/platform.c src/ui.c src/shots.c src/zk_png.c
GEN_SRCS := $(wildcard generated/*.c)
LVGL_CORE_SRCS := $(shell find $(LVGL_DIR)/src -name '*.c' \
	-not -path '*/drivers/*' \
	-not -path '*/sdl/*' \
	-not -path '*/x11/*' \
	-not -path '*/wayland/*' \
	-not -path '*/drm/*' \
	-not -path '*/nuttx/*' \
	-not -path '*/glfw/*' \
	-not -path '*/qnx/*' \
	-not -path '*/libinput/*' \
	2>/dev/null)
LVGL_DRV_SRCS := $(wildcard $(LVGL_DIR)/src/drivers/display/fb/*.c) \
	$(wildcard $(LVGL_DIR)/src/drivers/evdev/*.c)
LVGL_SRCS := $(sort $(LVGL_CORE_SRCS) $(LVGL_DRV_SRCS))
KIOSK_SRCS := $(APP_SRCS) $(GEN_SRCS) $(CJSON) $(LVGL_SRCS)

HOST_OBJDIR := $(BUILD)/host
ARM_OBJDIR := $(BUILD)/arm
HOST_OBJS := $(addprefix $(HOST_OBJDIR)/,$(KIOSK_SRCS:.c=.o))
ARM_OBJS := $(addprefix $(ARM_OBJDIR)/,$(KIOSK_SRCS:.c=.o))

ARCH_ARM := -march=armv7-a -mfpu=vfpv3-d16 -mfloat-abi=hard
OPT := -Os -ffunction-sections -fdata-sections
DEPFLAGS := -MMD -MP
STD := -std=gnu11
INC := -Isrc -I. -Ithird_party/cJSON -Ithird_party/stb -I$(LVGL_DIR) \
	-DLV_CONF_INCLUDE_SIMPLE -DLV_LVGL_H_INCLUDE_SIMPLE

HOST_APP_CFLAGS := $(STD) -Wall -Wextra -Werror $(OPT) $(INC)
HOST_EXT_CFLAGS := $(STD) -w $(OPT) $(INC)
ARM_APP_CFLAGS := $(ARCH_ARM) $(HOST_APP_CFLAGS)
ARM_EXT_CFLAGS := $(ARCH_ARM) $(HOST_EXT_CFLAGS)

.PHONY: test clean fetch-lvgl host arm arm-inner check e2e shots deploy-check

test: $(TEST_BINS)
	$(BUILD)/test_json
	$(BUILD)/test_logic
	$(BUILD)/test_layout
	$(BUILD)/test_http
	$(BUILD)/test_data
	$(BUILD)/test_stop_latency
	$(BUILD)/test_text
	$(BUILD)/test_ui_text
	$(BUILD)/test_power

$(BUILD):
	mkdir -p $(BUILD)

$(BUILD)/test_json: tests/test_json.c $(SRC_CORE) $(CJSON) | $(BUILD)
	$(CC) $(CFLAGS) $(SANITIZE) $(INCLUDES) -g -o $@ tests/test_json.c $(SRC_CORE) $(CJSON)

$(BUILD)/test_logic: tests/test_logic.c $(SRC_CORE) $(CJSON) | $(BUILD)
	$(CC) $(CFLAGS) $(SANITIZE) $(INCLUDES) -g -o $@ tests/test_logic.c $(SRC_CORE) $(CJSON)

$(BUILD)/test_layout: tests/test_layout.c $(SRC_CORE) $(CJSON) | $(BUILD)
	$(CC) $(CFLAGS) $(SANITIZE) $(INCLUDES) -g -o $@ tests/test_layout.c $(SRC_CORE) $(CJSON)

$(BUILD)/test_http: tests/test_http.c $(SRC_CORE) $(CJSON) | $(BUILD)
	$(CC) $(CFLAGS) $(SANITIZE) $(INCLUDES) -g -pthread -o $@ tests/test_http.c $(SRC_CORE) $(CJSON)

$(BUILD)/test_data: tests/test_data.c $(SRC_CORE) $(SRC_DATA) $(CJSON) | $(BUILD)
	$(CC) $(CFLAGS) $(SANITIZE) $(INCLUDES) -g -pthread -o $@ tests/test_data.c $(SRC_CORE) $(SRC_DATA) $(CJSON)

$(BUILD)/test_stop_latency: tests/test_stop_latency.c $(SRC_CORE) $(SRC_DATA) $(CJSON) | $(BUILD)
	$(CC) $(CFLAGS) $(SANITIZE) $(INCLUDES) -g -pthread -o $@ tests/test_stop_latency.c $(SRC_CORE) $(SRC_DATA) $(CJSON)

$(BUILD)/test_text: tests/test_text.c src/zk_text.c | $(BUILD)
	$(CC) $(CFLAGS) $(SANITIZE) $(INCLUDES) -g -o $@ tests/test_text.c src/zk_text.c

$(BUILD)/test_power: tests/test_power.c src/zk_power.c | $(BUILD)
	$(CC) $(CFLAGS) $(SANITIZE) $(INCLUDES) -g -o $@ tests/test_power.c src/zk_power.c

TESTUI_DIR := $(BUILD)/testui
TESTUI_SRCS := tests/test_ui_text.c $(SRC_CORE) $(SRC_DATA) src/platform.c src/ui.c src/zk_png.c $(GEN_SRCS) $(CJSON) $(LVGL_SRCS)
TESTUI_OBJS := $(addprefix $(TESTUI_DIR)/,$(TESTUI_SRCS:.c=.o))
TESTUI_APP_CFLAGS := $(STD) -Wall -Wextra -Werror -Os -g $(SANITIZE) $(INC)
TESTUI_EXT_CFLAGS := $(STD) -w -Os -g $(SANITIZE) $(INC)

$(BUILD)/test_ui_text: $(TESTUI_OBJS)
	@test -n "$(LVGL_SRCS)" || { echo "error: LVGL not fetched; run make fetch-lvgl" >&2; exit 1; }
	$(CC) $(SANITIZE) -o $@ $(TESTUI_OBJS) -lm -lpthread

$(TESTUI_DIR)/tests/%.o: tests/%.c
	@mkdir -p $(dir $@)
	$(CC) $(TESTUI_APP_CFLAGS) $(DEPFLAGS) -c -o $@ $<

$(TESTUI_DIR)/src/%.o: src/%.c
	@mkdir -p $(dir $@)
	$(CC) $(TESTUI_APP_CFLAGS) $(DEPFLAGS) -c -o $@ $<

$(TESTUI_DIR)/generated/%.o: generated/%.c
	@mkdir -p $(dir $@)
	$(CC) $(TESTUI_EXT_CFLAGS) $(DEPFLAGS) -c -o $@ $<

$(TESTUI_DIR)/third_party/%.o: third_party/%.c
	@mkdir -p $(dir $@)
	$(CC) $(TESTUI_EXT_CFLAGS) $(DEPFLAGS) -c -o $@ $<

$(TESTUI_DIR)/.cache/lvgl/%.o: .cache/lvgl/%.c
	@mkdir -p $(dir $@)
	$(CC) $(TESTUI_EXT_CFLAGS) $(DEPFLAGS) -c -o $@ $<

fetch-lvgl:
	tools/fetch-lvgl.sh

host: $(BUILD)/zan-kiosk-host

arm:
	docker run --rm -v $(CURDIR):/w -w /w debian:bookworm-slim bash -c 'apt-get update -qq && apt-get install -y -qq gcc-arm-linux-gnueabihf make >/dev/null && make -j$$(nproc) arm-inner'

arm-inner: $(BUILD)/zan-kiosk-arm

check: test e2e deploy-check

deploy-check:
	bash tests/deploy_test.sh

e2e: $(BUILD)/zan-kiosk-host
	bash tests/e2e.sh

shots: $(BUILD)/zan-kiosk-host
	mkdir -p $(BUILD)/shots
	$(BUILD)/zan-kiosk-host --shot-all $(BUILD)/shots --fixtures-root tests/fixtures

$(BUILD)/zan-kiosk-host: $(HOST_OBJS)
	@test -n "$(LVGL_SRCS)" || { echo "error: LVGL not fetched; run make fetch-lvgl" >&2; exit 1; }
	$(HOST_CC) $(OPT) -Wl,--gc-sections -o $@ $(HOST_OBJS) -lm -lpthread

$(BUILD)/zan-kiosk-arm: $(ARM_OBJS)
	@test -n "$(LVGL_SRCS)" || { echo "error: LVGL not fetched; run make fetch-lvgl" >&2; exit 1; }
	$(ARM_CC) $(ARCH_ARM) $(OPT) -static -Wl,--gc-sections -s -o $@ $(ARM_OBJS) -lm -lpthread

$(HOST_OBJDIR)/src/%.o: src/%.c
	@mkdir -p $(dir $@)
	$(HOST_CC) $(HOST_APP_CFLAGS) $(DEPFLAGS) -c -o $@ $<

$(HOST_OBJDIR)/generated/%.o: generated/%.c
	@mkdir -p $(dir $@)
	$(HOST_CC) $(HOST_EXT_CFLAGS) $(DEPFLAGS) -c -o $@ $<

$(HOST_OBJDIR)/third_party/%.o: third_party/%.c
	@mkdir -p $(dir $@)
	$(HOST_CC) $(HOST_EXT_CFLAGS) $(DEPFLAGS) -c -o $@ $<

$(HOST_OBJDIR)/.cache/lvgl/%.o: .cache/lvgl/%.c
	@mkdir -p $(dir $@)
	$(HOST_CC) $(HOST_EXT_CFLAGS) $(DEPFLAGS) -c -o $@ $<

$(ARM_OBJDIR)/src/%.o: src/%.c
	@mkdir -p $(dir $@)
	$(ARM_CC) $(ARM_APP_CFLAGS) $(DEPFLAGS) -c -o $@ $<

$(ARM_OBJDIR)/generated/%.o: generated/%.c
	@mkdir -p $(dir $@)
	$(ARM_CC) $(ARM_EXT_CFLAGS) $(DEPFLAGS) -c -o $@ $<

$(ARM_OBJDIR)/third_party/%.o: third_party/%.c
	@mkdir -p $(dir $@)
	$(ARM_CC) $(ARM_EXT_CFLAGS) $(DEPFLAGS) -c -o $@ $<

$(ARM_OBJDIR)/.cache/lvgl/%.o: .cache/lvgl/%.c
	@mkdir -p $(dir $@)
	$(ARM_CC) $(ARM_EXT_CFLAGS) $(DEPFLAGS) -c -o $@ $<

clean:
	rm -rf $(BUILD)

-include $(HOST_OBJS:.o=.d)
-include $(ARM_OBJS:.o=.d)
-include $(TESTUI_OBJS:.o=.d)
